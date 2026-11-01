#include "Commander/Behaviors/PullBehavior.h"
#include "Commander/Coordination/PullPlanner.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/PackResolver.h"
#include "PartyBotAI.h"
#include "DBCStores.h"
#include "Map.h"
#include "SpellMgr.h"
#include "Timer.h"
#include <algorithm>
#include <cmath>

namespace Commander
{
    namespace
    {
        enum class ShotStep : uint8
        {
            Shoot,      // in position: shoot (again)
            Wait,       // walking the retry step
            Retry,      // no fight after the shot: step closer, once
            WalkIn,     // no fight after the retry either: walk-in pull
        };

        // Called in range with line of sight. `walking` = the tank is still on a held walk.
        // Keeps its state in PullPlan::rangedShot (mutable, see TeamPlan.h): only the tank's
        // tick calls this, on the commander's map thread. A new pull resets it with the plan
        // (one target per plan), a new Approach (held back to WaitReady and out again) here.
        ShotStep NextShotStep(PullPlan const& pull, bool targetInCombat, bool walking, uint32 nowMs)
        {
            uint32 const noCombatMs = PullBehavior::RANGED_NO_COMBAT_MS;
            PullShotState& s = pull.rangedShot;
            if (s.approachSinceMs != pull.phaseSinceMs)
            {
                s = PullShotState();
                s.approachSinceMs = pull.phaseSinceMs;
            }

            if (s.walkIn)
                return ShotStep::WalkIn;
            if (s.retried && walking)
            {
                s.shotAtMs = nowMs;     // the retry window starts once the tank stands
                return ShotStep::Wait;
            }
            // Fight started: PullAssigner moves on with the next refresh.
            if (!s.shotAtMs || targetInCombat || WorldTimer::getMSTimeDiff(s.shotAtMs, nowMs) < noCombatMs)
            {
                if (!s.shotAtMs)
                    s.shotAtMs = nowMs;
                return ShotStep::Shoot;
            }
            if (!s.retried)
            {
                s.retried = true;
                s.shotAtMs = nowMs;
                return ShotStep::Retry;
            }
            s.walkIn = true;
            return ShotStep::WalkIn;
        }

        Proposal Make(ProposalTier tier, float score, BotActionType type, ObjectGuid target = ObjectGuid())
        {
            Proposal p;
            p.tier = tier;
            p.score = score;
            p.source = "pull";
            p.action.type = type;
            p.action.target = target;
            return p;
        }

        Position3 PositionOf(WorldObject const* obj)
        {
            Position3 p;
            p.x = obj->GetPositionX();
            p.y = obj->GetPositionY();
            p.z = obj->GetPositionZ();
            return p;
        }

        Proposal MoveTo(Position3 const& to, float distance)
        {
            Proposal p = Make(ProposalTier::Assigned, 5.0f, BotActionType::MoveTo);
            p.action.x = to.x;
            p.action.y = to.y;
            p.action.z = to.z;
            p.action.holdMs = uint32(distance / PullBehavior::RUN_SPEED_YD_PER_SEC * 1000.0f) + 500;
            return p;
        }

        // Ranged pull (PullPlan::rangedPullSpell). Returns false when the tank should attack
        // normally instead (spell gone, already closer than the spell's minimum range).
        bool ProposeRangedPull(PartyBotAI const* bot, PullPlan const& pull, Unit* target, std::vector<Proposal>& out)
        {
            Player* me = bot->me;
            SpellEntry const* spell = sSpellMgr.GetSpellEntry(pull.rangedPullSpell);
            if (!spell)
                return false;
            SpellRangeEntry const* srange = sSpellRangeStore.LookupEntry(spell->rangeIndex);
            float const minRange = srange ? srange->minRange : 0.0f;
            float const distance = me->GetCombatDistance(target);
            if (minRange > 0.0f && distance < minRange)
                return false;

            // Shot on its way (cast time): let it finish.
            if (me->IsNonMeleeSpellCasted(false, false, true))
            {
                out.push_back(Make(ProposalTier::Assigned, 5.0f, BotActionType::Hold));
                return true;
            }

            bool const inRange = distance <= pull.rangedPullRange;
            if (inRange && me->IsWithinLOSInMap(target))
            {
                bool const walking = me->IsMoving() && bot->IsMovementHeld();
                switch (NextShotStep(pull, target->IsInCombat(), walking, WorldTimer::getMSTime()))
                {
                    case ShotStep::WalkIn:
                        return false;
                    case ShotStep::Wait:
                        out.push_back(Make(ProposalTier::Assigned, 5.0f, BotActionType::Hold));
                        return true;
                    case ShotStep::Retry:
                    {
                        // A few yards closer (not inside the minimum range), then shoot again.
                        float const retryStep = PullBehavior::RANGED_RETRY_STEP_YD;
                        Position3 const here = PositionOf(me);
                        Position3 const there = PositionOf(target);
                        float const total = PackResolver::Distance(here, there);
                        float const step = std::min(retryStep, distance - (minRange + 1.0f));
                        if (step >= 1.0f)
                        {
                            float const f = step / std::max(total, 0.1f);
                            Position3 to;
                            to.x = here.x + (there.x - here.x) * f;
                            to.y = here.y + (there.y - here.y) * f;
                            to.z = here.z + (there.z - here.z) * f;
                            out.push_back(MoveTo(to, step));
                            return true;
                        }
                        break;      // no room to step closer: just shoot again
                    }
                    case ShotStep::Shoot:
                        break;
                }
                Proposal cast = Make(ProposalTier::Assigned, 5.0f, BotActionType::Cast, pull.target);
                cast.action.spell = spell;
                out.push_back(cast);
                // Not castable this tick (GCD, cooldown, form): stand still rather than letting the
                // original AI run in. PullAssigner falls back to a walk-in pull after a while.
                out.push_back(Make(ProposalTier::Assigned, 4.0f, BotActionType::Hold));
                return true;
            }

            // Still walking: do not restart the spline every tick.
            if (me->IsMoving() && bot->IsMovementHeld())
            {
                out.push_back(Make(ProposalTier::Assigned, 5.0f, BotActionType::Hold));
                return true;
            }

            Position3 const here = PositionOf(me);
            Position3 const there = PositionOf(target);
            if (inRange)
            {
                // In range but no line of sight: step towards the target, then look again.
                float const step = std::min(PullBehavior::LOS_STEP_YD, PackResolver::Distance(here, there));
                float const f = step / std::max(PackResolver::Distance(here, there), 0.1f);
                Position3 to;
                to.x = here.x + (there.x - here.x) * f;
                to.y = here.y + (there.y - here.y) * f;
                to.z = here.z + (there.z - here.z) * f;
                out.push_back(MoveTo(to, step));
                return true;
            }

            // Walk to a point inside the range on the line to the target: the tank stops there
            // instead of running into the pack (a hold left from walking would delay Return).
            float const total = PackResolver::Distance(here, there);
            float const keep = std::max(pull.rangedPullRange - PullPlanner::RANGED_STAND_MARGIN, minRange + 1.0f);
            float const walk = std::max(total - keep, 1.0f);
            float const f = walk / std::max(total, 0.1f);
            Position3 to;
            to.x = here.x + (there.x - here.x) * f;
            to.y = here.y + (there.y - here.y) * f;
            to.z = here.z + (there.z - here.z) * f;
            out.push_back(MoveTo(to, walk));
            return true;
        }
    }

    bool PullBehavior::AppliesTo(PartyBotAI const* bot) const
    {
        return bot->GetRole() != ROLE_INVALID;
    }

    void PullBehavior::Propose(PartyBotAI const* bot, WorldModel const& /*world*/,
                               TeamPlan const& plan, std::vector<Proposal>& out) const
    {
        PullPlan const& pull = plan.pull;
        if (pull.phase == PullPhase::None || pull.phase == PullPhase::WaitReady)
            return;

        Player* me = bot->me;

        if (me->GetObjectGuid() == pull.tank)
        {
            if (pull.phase == PullPhase::Approach)
            {
                Unit* target = me->GetMap()->GetUnit(pull.target);
                if (!target || !target->IsAlive())
                    return;
                if (pull.rangedPullSpell && ProposeRangedPull(bot, pull, target, out))
                    return;
                if (me->GetVictim() != target)
                    out.push_back(Make(ProposalTier::Assigned, 5.0f, BotActionType::Attack, pull.target));
            }
            else if (pull.phase == PullPhase::Return && !bot->IsMovementHeld())
            {
                float const distance = PackResolver::Distance(PositionOf(me), pull.spot);
                out.push_back(MoveTo(pull.spot, distance));
            }
            else if (pull.phase == PullPhase::Settle && pull.losBreak == PullLosBreak::Active && !bot->IsMovementHeld())
            {
                // Line of sight pull: walk to the point and stay there. The hold is renewed
                // (a MoveTo to the point it stands on) whenever it runs out, so the original
                // chase does not take the tank back into the caster's sight. Without a hold
                // the original AI still fights from the point (melee, spells), it only does not
                // chase. Ends when PullAssigner ends the break (at most LOS_BREAK_TIMEOUT_MS),
                // the last hold runs out LOS_HOLD_EXTRA_MS later.
                float const distance = PackResolver::Distance(PositionOf(me), pull.losPoint);
                Proposal p = MoveTo(pull.losPoint, distance);
                p.action.holdMs += PullBehavior::LOS_HOLD_EXTRA_MS;
                out.push_back(p);
            }
            return;
        }

        // Everyone else waits for the tank. Higher-scored proposals (interrupts, threat) still win.
        // Only out of combat: a bot already in a fight (the player attacked first, a patrol joined)
        // must not stand idle for up to 20 seconds. Waiting for first threat is the threat module's job.
        if (me->IsInCombat())
            return;
        // Settle holds for SETTLE_MS only. A line of sight pull keeps Settle going for up to
        // LOS_BREAK_TIMEOUT_MS more; a healer not in combat yet must be free to heal the tank then.
        uint32 const settleMs = PullAssigner::SETTLE_MS;
        if (pull.phase == PullPhase::Settle && WorldTimer::getMSTimeDiff(pull.phaseSinceMs, WorldTimer::getMSTime()) >= settleMs)
            return;
        out.push_back(Make(ProposalTier::Assigned, 1.0f, BotActionType::Hold));
    }
}
