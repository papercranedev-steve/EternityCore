#include "Commander/Behaviors/PullBehavior.h"
#include "Commander/Coordination/PullPlanner.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Classes/ClassKit.h"
#include "Commander/Perception/AggroRange.h"
#include "Commander/Perception/PackResolver.h"
#include "Commander/Perception/ShotSpot.h"
#include "Commander/BotDirectives.h"
#include "PartyBotAI.h"
#include "Creature.h"
#include "DBCStores.h"
#include "Map.h"
#include "SpellMgr.h"
#include "Timer.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace Commander
{
    namespace
    {
        enum class ShotStep : uint8
        {
            Shoot,      // in position: shoot (again)
            Retry,      // no fight after the shot: step closer, once
            WalkIn,     // no fight after the retry either: walk-in pull
        };

        // The shot steps of this Approach, kept in PullPlan::rangedShot (mutable, see
        // TeamPlan.h): only the tank's tick writes it, on the commander's map thread. A new
        // pull resets it with the plan, a new Approach (held back to WaitReady and out again,
        // or a new spot or target from the brain) here.
        PullShotState& ShotState(PullPlan const& pull)
        {
            PullShotState& s = pull.rangedShot;
            if (s.approachSinceMs != pull.phaseSinceMs)
            {
                s = PullShotState();
                s.approachSinceMs = pull.phaseSinceMs;
                s.standAt = pull.facePull ? pull.faceSpot : pull.rangedSpot;
            }
            return s;
        }

        // Called on the stand point, in range, with line of sight.
        ShotStep NextShotStep(PullShotState& s, bool targetInCombat, uint32 nowMs)
        {
            uint32 const noCombatMs = PullBehavior::RANGED_NO_COMBAT_MS;
            if (s.walkIn)
                return ShotStep::WalkIn;
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

            // On the point the order only keeps the tank there: area threat and taunts of the
            // threat module come first (strat-20261107-143623: the stay order at 5.0 beat them
            // for 7 s while the pack stood on the tank at 0 threat).
            float const left = PackResolver::Distance(PositionOf(me), to);
            bool const onPoint = left <= PullBehavior::POINT_ARRIVED_YD;
            Proposal back = Make(ProposalTier::Assigned, onPoint ? PullBehavior::STAY_SCORE : 5.0f,
                                 BotActionType::Backpedal, face);
            back.action.x = to.x;
            back.action.y = to.y;
            back.action.z = to.z;
            back.action.holdMs = PullBehavior::BACKPEDAL_ORDER_MS;
            out.push_back(back);

            if (!onPoint && !bot->IsMovementHeld())
            {
                Proposal forward = MoveTo(to, left);
                forward.score = 4.0f;
                out.push_back(forward);
            }
        }

        // No Charge / Intercept while the tank goes out to bring the pack back or to take it to
        // a corner.
        Proposal NoCharge(char const* reason = "bringing the pack back")
        {
            Proposal p;
            p.kind = ProposalKind::Veto;
            p.tier = ProposalTier::Assigned;
            p.source = "pull";
            p.reason = reason;
            p.veto.type = VetoType::GapCloser;
            return p;
        }

        void SpotFailed(PullShotState& s, char const* why)
        {
            s.spotFailed = true;
            s.spotFailWhy = why;
        }

        float Distance2d(Position3 const& a, Position3 const& b)
        {
            float const dx = a.x - b.x, dy = a.y - b.y;
            return std::sqrt(dx * dx + dy * dy);
        }

        enum class StandWalk : uint8
        {
            Arrived,
            Walking,    // a proposal was pushed
            Failed,     // not there in time (SpotFailed set); nothing pushed
        };

        // Walk to PullShotState::standAt (pathfinding), shared by the shot and the face pull.
        StandWalk WalkToStand(PartyBotAI const* bot, PullShotState& s, uint32 now, std::vector<Proposal>& out)
        {
            Player* me = bot->me;
            Position3 const here = PositionOf(me);
            float const toStand = Distance2d(here, s.standAt);
            bool const arrived = toStand <= PullBehavior::SPOT_ARRIVED_YD ||
                (toStand <= PullBehavior::SPOT_NEAR_YD && !me->IsMoving());
            if (arrived)
                return StandWalk::Arrived;
            // The retry window starts once the tank stands.
            if (s.retried)
                s.shotAtMs = now;
            if (!s.walkSinceMs)
            {
                s.walkSinceMs = now;
                s.walkBudgetMs = uint32(toStand / PullBehavior::RUN_SPEED_YD_PER_SEC * 1000.0f *
                                        PullBehavior::SPOT_WALK_TIME_FACTOR) + PullBehavior::SPOT_WALK_EXTRA_MS;
            }
            else if (WorldTimer::getMSTimeDiff(s.walkSinceMs, now) > s.walkBudgetMs)
            {
                SpotFailed(s, "cannot get to the spot");
                return StandWalk::Failed;
            }
            // Still walking: do not restart the spline every tick.
            if (me->IsMoving() && bot->IsMovementHeld())
                out.push_back(Make(ProposalTier::Assigned, 5.0f, BotActionType::Hold));
            else
                out.push_back(MoveTo(s.standAt, toStand * PullBehavior::SPOT_WALK_TIME_FACTOR));
            return StandWalk::Walking;
        }

        // The opener went out this Approach (PullShotState::openedAtMs): a pull proposal cast
        // (the pull spell, Charge) executed since the Approach began, or the tank stands inside
        // the target's aggro range (walk-in, face pull: it notices the tank).
        void NoteOpened(PartyBotAI const* bot, PullPlan const& pull, Unit const* target, PullShotState& s, uint32 now)
        {
            if (s.openedAtMs)
                return;
            ExecutedAction const& last = bot->Directives().GetLastExecuted();
            bool const cast = last.timeMs && last.type == BotActionType::Cast && !strcmp(last.source, "pull") &&
                WorldTimer::getMSTimeDiff(pull.phaseSinceMs, last.timeMs) <= WorldTimer::getMSTimeDiff(pull.phaseSinceMs, now);
            bool inReach = false;
            if (Creature const* c = target->ToCreature())
                inReach = AggroRange::Covers(c, AggroRange::Reach(c, bot->me, 0.0f), PositionOf(bot->me));
            if (cast || inReach)
                s.openedAtMs = now;
        }

        // Face pull (PullPlan::facePull): walk to the spot, then stand there doing nothing (a
        // Hold: the rotation does not run, so no attack or chase starts) until the target
        // notices. Returns false when the tank should walk in instead (not there in time, not
        // noticed within FACE_WAIT_MS).
        bool ProposeFacePull(PartyBotAI const* bot, PullPlan const& pull, Unit* target, std::vector<Proposal>& out)
        {
            uint32 const now = WorldTimer::getMSTime();
            PullShotState& s = ShotState(pull);
            if (s.walkIn)
                return false;
            switch (WalkToStand(bot, s, now, out))
            {
                case StandWalk::Walking:
                    return true;
                case StandWalk::Failed:
                    s.walkIn = true;
                    return false;
                case StandWalk::Arrived:
                    break;
            }
            if (!s.shotAtMs)
                s.shotAtMs = now;
            if (!s.openedAtMs)
                s.openedAtMs = now;
            if (!target->IsInCombat() && WorldTimer::getMSTimeDiff(s.shotAtMs, now) > PullBehavior::FACE_WAIT_MS)
            {
                s.walkIn = true;
                return false;
            }
            out.push_back(Make(ProposalTier::Assigned, 5.0f, BotActionType::Hold));
            return true;
        }

        // Ranged pull (PullPlan::rangedPullSpell): walk to the shot spot the brain picked
        // (PullPlan::rangedSpot, Perception/ShotSpot.h), then shoot. Returns false when the tank
        // should attack normally instead (spell gone, already closer than the spell's minimum
        // range, the shot did not start the fight after the retry).
        // The tank never steps closer to find line of sight: on the spot without sight or out
        // of range (the target wandered), or not there in time, it holds and reports
        // (PullShotState::spotFailed); the brain answers with another spot or target.
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

            uint32 const now = WorldTimer::getMSTime();
            PullShotState& s = ShotState(pull);
            if (s.walkIn)
                return false;
            // Waiting for the brain's answer (next refresh).
            if (s.spotFailed)
            {
                out.push_back(Make(ProposalTier::Assigned, 5.0f, BotActionType::Hold));
                return true;
            }

            // Not on the stand point yet: walk there (pathfinding).
            switch (WalkToStand(bot, s, now, out))
            {
                case StandWalk::Walking:
                    return true;
                case StandWalk::Failed:
                    out.push_back(Make(ProposalTier::Assigned, 5.0f, BotActionType::Hold));
                    return true;
                case StandWalk::Arrived:
                    break;
            }
            Position3 const here = PositionOf(me);

            if (distance > pull.rangedPullRange)
            {
                SpotFailed(s, "out of range from the spot");
                out.push_back(Make(ProposalTier::Assigned, 5.0f, BotActionType::Hold));
                return true;
            }
            if (!me->IsWithinLOSInMap(target))
            {
                SpotFailed(s, "no line of sight from the spot");
                out.push_back(Make(ProposalTier::Assigned, 5.0f, BotActionType::Hold));
                return true;
            }

            switch (NextShotStep(s, target->IsInCombat(), now))
            {
                case ShotStep::WalkIn:
                    return false;
                case ShotStep::Retry:
                {
                    // A few yards closer (not inside the minimum range, not into an idle
                    // creature's aggro range), then shoot again; no such step: shoot again here.
                    Position3 const there = PositionOf(target);
                    float const total = PackResolver::Distance(here, there);
                    float const step = std::min(PullBehavior::RANGED_RETRY_STEP_YD, distance - (minRange + 1.0f));
                    if (step >= 1.0f)
                    {
                        float const f = step / std::max(total, 0.1f);
                        Position3 to;
                        to.x = here.x + (there.x - here.x) * f;
                        to.y = here.y + (there.y - here.y) * f;
                        to.z = here.z + (there.z - here.z) * f;
                        Creature const* creature = target->ToCreature();
                        if (creature && ShotSpot::IsClear(creature, me, to))
                        {
                            s.standAt = to;
                            s.walkSinceMs = 0;
                            out.push_back(MoveTo(to, step));
                            return true;
                        }
                    }
                    break;
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
    }

    bool PullBehavior::AppliesTo(PartyBotAI const* bot) const
    {
        return bot->GetRole() != ROLE_INVALID;
    }

    void PullBehavior::Propose(PartyBotAI const* bot, WorldModel const& world,
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
                // Opening control (PullControl.h): wait until it is decided and the
                // controller stands ready, so both go together.
                if (pull.control.HoldsOpener() && !me->IsInCombat())
                {
                    out.push_back(Make(ProposalTier::Assigned, 5.0f, BotActionType::Hold));
                    return;
                }
                NoteOpened(bot, pull, target, ShotState(pull), WorldTimer::getMSTime());
                if (pull.returnToSpot)
                    out.push_back(NoCharge());
                else if (pull.corner)
                    out.push_back(NoCharge("taking the pack to a corner"));
                if (pull.rangedPullSpell && ProposeRangedPull(bot, pull, target, out))
                    return;
                if (pull.facePull && ProposeFacePull(bot, pull, target, out))
                    return;
                if (me->GetVictim() != target)
                    out.push_back(Make(ProposalTier::Assigned, 5.0f, BotActionType::Attack, pull.target));
                // Walk-in pull fought where the pack stands: charge in once it is the victim
                // (ClassKit::OpenerCharge; a stance swap first when needed). Bringing the pack
                // back or to a corner gains nothing from it: the tank would only have to walk
                // back further.
                else if (!pull.returnToSpot && !pull.corner)
                    if (SpellEntry const* charge = bot->Kit().OpenerCharge())
                        if (bot->CanCastWithStance(target, charge))
                        {
                            Proposal p = Make(ProposalTier::Assigned, 5.0f, BotActionType::Cast, pull.target);
                            p.action.spell = charge;
                            out.push_back(p);
                        }
            }
            else if (pull.phase == PullPhase::Return)
            {
                // Backwards, facing the pack: parry, block and swings keep working, the rotation
                // keeps building threat (instant abilities), nothing hits the tank's back.
                out.push_back(NoCharge());
                WalkBack(bot, pull, pull.spot, out);
            }
            else if (pull.phase == PullPhase::Settle && pull.awaitArrival && pull.losBreak != PullLosBreak::Active)
            {
                // Ranged pull fought in place: stay where the shot was fired, facing the pack,
                // until it came (PullAssigner). Walking to it met the pack halfway, next to the
                // creatures the shot was meant to leave alone. The rotation keeps working (the
                // backpedal order lets it run); its chase is held.
                out.push_back(NoCharge());
                WalkBack(bot, pull, pull.arrivalPoint, out);
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

        bool const healer = bot->GetRole() == ROLE_HEALER;

        // The opening controller (PullControl.h) walks to its spot and casts as the fight
        // starts (CrowdControlBehavior): not held here until its control is cast.
        if (pull.control.Active() && pull.control.bot == me->GetObjectGuid() &&
            (pull.phase == PullPhase::Approach || plan.FindControlFor(me->GetObjectGuid())))
            return;

        // A healer hiding behind the tank from the casters of a line of sight pull
        // (PullPlan::losHiders): walk there, in combat or not. The cast under way finishes first.
        if (PullHider const* hider = pull.LosHiderOf(me->GetObjectGuid()))
        {
            float const left = PackResolver::Distance(PositionOf(me), hider->point);
            if (left > PullBehavior::POINT_ARRIVED_YD && !me->IsNonMeleeSpellCasted(false, false, true) &&
                !me->HasUnitState(UNIT_STATE_ROOT))
            {
                // Already walking there: keep walking (a cast-time heal would stop the walk).
                if (bot->IsMovementHeld() && me->IsMoving())
                    out.push_back(Make(ProposalTier::Assigned, 5.0f, BotActionType::Hold));
                else
                    out.push_back(MoveTo(hider->point, left));
                return;
            }
        }

        // Everyone else waits for the tank. Higher-scored proposals (interrupts, threat) still win.
        // Only out of combat: a bot already in a fight (the player attacked first, a patrol joined)
        // must not stand idle. In Settle, past the arrival wait, the brain says who still waits
        // (PullOpening.h): damage dealers until the tank has threat on what came, healers also
        // until the casters of a line of sight pull came.
        if (me->IsInCombat() || !pull.HoldsWaiting(healer))
            return;
        // Someone about to die: heal now (the rotation's heal step runs without the hold).
        if (healer && pull.phase == PullPhase::Settle && !pull.awaitArrival &&
            std::any_of(world.party.begin(), world.party.end(), [](PartyMemberFacts const& m)
            {
                return m.alive && !m.isPet && m.healthPct < PullBehavior::HOLD_HEAL_BELOW_PCT;
            }))
            return;
        out.push_back(Make(ProposalTier::Assigned, 1.0f, BotActionType::Hold));
    }
}
