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

        // Walk backwards to `to` facing the pack (Backpedal.h), then the forward walk as a
        // fallback (the arbiter tries the next proposal when no path is found). Proposed every
        // tick: the order lasts BACKPEDAL_ORDER_MS and is renewed.
        void WalkBack(PartyBotAI const* bot, PullPlan const& pull, Position3 const& to, std::vector<Proposal>& out)
        {
            Player* me = bot->me;
            ObjectGuid face = pull.target;
            Unit* target = me->GetMap()->GetUnit(face);
            if (!target || !target->IsAlive())
                face = me->GetVictim() ? me->GetVictim()->GetObjectGuid() : ObjectGuid();

            Proposal back = Make(ProposalTier::Assigned, 5.0f, BotActionType::Backpedal, face);
            back.action.x = to.x;
            back.action.y = to.y;
            back.action.z = to.z;
            back.action.holdMs = PullBehavior::BACKPEDAL_ORDER_MS;
            out.push_back(back);

            if (!bot->IsMovementHeld())
            {
                Proposal forward = MoveTo(to, PackResolver::Distance(PositionOf(me), to));
                forward.score = 4.0f;
                out.push_back(forward);
            }
        }

        // No Charge / Intercept while the tank goes out to bring the pack back.
        Proposal NoCharge()
        {
            Proposal p;
            p.kind = ProposalKind::Veto;
            p.tier = ProposalTier::Assigned;
            p.source = "pull";
            p.reason = "bringing the pack back";
            p.veto.type = VetoType::GapCloser;
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
                if (pull.returnToSpot)
                    out.push_back(NoCharge());
                if (pull.rangedPullSpell && ProposeRangedPull(bot, pull, target, out))
                    return;
                if (me->GetVictim() != target)
                    out.push_back(Make(ProposalTier::Assigned, 5.0f, BotActionType::Attack, pull.target));
            }
            else if (pull.phase == PullPhase::Return)
            {
                // Backwards, facing the pack: parry, block and swings keep working, the rotation
                // keeps building threat (instant abilities), nothing hits the tank's back.
                out.push_back(NoCharge());
                WalkBack(bot, pull, pull.spot, out);
            }
            else if (pull.phase == PullPhase::Settle && pull.losBreak == PullLosBreak::Active)
            {
                // Line of sight pull: walk (backwards) to the point and stay there. The order is
                // renewed every tick, standing on the point too (Backpedal::Order holds it), so
                // the original chase does not take the tank back into the caster's sight. The
                // original AI still fights from the point (melee, spells), it only does not
                // chase. Ends when PullAssigner ends the break (at most LOS_BREAK_TIMEOUT_MS),
                // the last order runs out BACKPEDAL_ORDER_MS later.
                WalkBack(bot, pull, pull.losPoint, out);
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
