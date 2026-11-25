#include "Commander/Behaviors/Movement/RangedStanceBehavior.h"
#include "Commander/Behaviors/Movement/RangedSpot.h"
#include "Commander/Perception/Spatial/SafeRoute.h"
#include "Commander/Perception/Values/ValueModel.h"
#include "Commander/Perception/Values/Exposure.h"
#include "Commander/Perception/Values/SpotCost.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Coordination/Situation/Patrol/RouteGuard.h"
#include "Commander/Perception/Spatial/PositionField.h"
#include "Commander/Perception/Spatial/SightCache.h"
#include "Commander/Perception/Values/SightMargin.h"
#include "Commander/Behaviors/Movement/JoinGamble.h"
#include "Commander/Classes/ClassKit.h"
#include "Commander/Trace/Trace.h"
#include "TeamBotAI.h"
#include "Map.h"
#include <algorithm>
#include <cmath>

namespace Commander
{
    namespace
    {
        using SafeRoute::Distance2d;

        Position3 PositionOf(WorldObject const* obj)
        {
            Position3 p;
            p.x = obj->GetPositionX();
            p.y = obj->GetPositionY();
            p.z = obj->GetPositionZ();
            return p;
        }
    }

    bool RangedStanceBehavior::AppliesTo(TeamBotAI const* bot) const
    {
        return bot->GetRole() == ROLE_RANGE_DPS;
    }

    bool RangedStanceBehavior::TakesOver(Takeover what, TeamBotAI const* bot, WorldModel const& world,
                                         TeamPlan const& plan) const
    {
        // Before the fight too (the place for the planned fight, Propose's `prepare`): else the
        // leader follow started again once the walk's hold ran out and took it back, every
        // ~1.1 s while the pull waited (Stratholme 2026-11-25, hunter and warlock).
        bool const inCombat = bot->me->IsInCombat();
        Position3 planned;
        return what == Takeover::Stance && !bot->me->GetTransport() &&
               (world.InFight(inCombat) || (!inCombat && plan.PlannedFightSpot(planned)));
    }

    void RangedStanceBehavior::Propose(TeamBotAI const* bot, WorldModel const& world,
                                       TeamPlan const& plan, std::vector<Proposal>& out) const
    {
        // The retreat owns movement (Override tier).
        if (plan.retreat.active)
            return;
        Player* me = bot->me;
        if (me->HasUnitState(UNIT_STATE_ROOT) || !bot->UsesCasterChase())
            return;
        // Hiding from a caster standing back (LureBehavior walks it).
        if (plan.HidingFor(me->GetObjectGuid()))
            return;

        // Before the fight: out of combat while the pull runs, take a place in cast range of
        // where the brain planned the tank to fight (PullPlan::PlannedFightSpot), then wait.
        bool const inCombat = me->IsInCombat();
        Position3 planned;
        bool const prepare = !inCombat && plan.PlannedFightSpot(planned);
        if (!prepare)
        {
            // In combat the pull owns movement while the tank goes out and brings the pack back.
            if (plan.pull.phase == PullPhase::Approach || plan.pull.phase == PullPhase::Return)
                return;
            if (!world.InFight(inCombat))
                return;
            // Still out of combat and told to wait by the pull (opening hold).
            if (!inCombat && plan.pull.HoldsWaiting(false))
                return;
        }

        // A MoveTo (this module's or another's) is under way: keep walking. Without a proposal
        // the rotation's first cast stops the walk (DoCastSpell -> StopMoving).
        if (bot->IsMovementHeld() && me->IsMoving())
        {
            Proposal hold;
            hold.tier = ProposalTier::Assigned;
            hold.score = 0.0f;
            hold.source = "rangedstance";
            hold.reason = "walking to a spot, keep walking";
            hold.action.type = BotActionType::Hold;
            out.push_back(hold);
            return;
        }
        if (me->IsNonMeleeSpellCasted(false, false, true))
            return;

        PartyMemberFacts const* self = world.FindMember(me->GetObjectGuid());
        if (!self || !self->CanAct())
            return;
        // The area phase places its caster (AreaSpot.h, 2026-11-12): before the fight nothing
        // here; in the fight only an idle creature's reach moves it (Urgent), not its victim
        // out of range (the spell's centre is the area phase's choice).
        bool const placed = plan.area.PlacesCaster(self->guid);
        if (placed && prepare)
            return;
        Position3 const here = PositionOf(me);
        Position3 at;               // what the bot must reach: its victim, or the planned fight spot
        ObjectGuid atGuid;          // the victim (empty before the fight)
        float atReach = 0.0f;
        bool inReach = false;
        // Its attack's dead zone (hunter Auto Shot, ClassKit::MinAttackRange): a spot this close
        // to the victim's edge is out of reach too (user 2026-11-15), cells start beyond it.
        float const deadZone = bot->Kit().MinAttackRange();
        bool inDeadZone = false;
        if (prepare)
        {
            at = planned;
            inDeadZone = deadZone > 0.0f && Distance2d(here, at) < deadZone;
            bool const inRange = Distance2d(here, at) <= CAST_RANGE && !inDeadZone;
            inReach = inRange && SightCache::Live(me, here, at);
            if (inRange && !inReach && world.field && world.field->sight)
                world.field->sight->MarkStandFailed(me, at);    // (below)
        }
        else
        {
            // No victim yet: the target the brain gave it. TargetBehavior only attacks it in
            // range and sight, so without this the bot stood where it was (it no longer follows
            // the leader during the party's fight).
            Unit* victim = me->GetVictim();
            if (!victim)
                if (TargetDuty const* duty = plan.FindTargetFor(me->GetObjectGuid()))
                    victim = me->GetMap()->GetUnit(duty->enemy);
            if (!victim || !victim->IsAlive())
                return;
            // It comes for the bot: running from melee is the escape and kite modules' job.
            if (victim->GetVictim() == me)
                return;
            at = PositionOf(victim);
            atGuid = victim->GetObjectGuid();
            atReach = victim->GetCombatReach();
            // (the spell's line: SightCache::Live)
            float const distance = me->GetCombatDistance(victim);
            inDeadZone = deadZone > 0.0f && distance < deadZone;
            inReach = distance <= CAST_RANGE && !inDeadZone && SightCache::Live(me, here, at);
            // In range but the line fails where it stands (not walking, above): the spot search
            // does not take this cell again for this victim (SightCache::StandFailed,
            // stockade-20261125-221620).
            if (!inReach && distance <= CAST_RANGE && !inDeadZone && world.field && world.field->sight)
                world.field->sight->MarkStandFailed(me, at);
        }

        SafeRoute::IdleCreatures const idle(me);
        // In the fight a circle covering the victim's spot counts only closer than the victim
        // (FieldSpots.h): a spot taken by that rule is not left again next tick.
        bool const unsafe = (prepare ? idle : idle.ArrivingAt(at)).Near(here);
        // The enemies' exposure (2026-11-10, user: squishy casters stand well back;
        // Perception/Values/Exposure.h). Not before the fight, not for the area caster: the
        // area phase picked where it casts from.
        bool const useExposure = !prepare && !placed;
        std::vector<EnemyFacts const*> const turnable =
            useExposure ? Exposure::Turnable(world, self->guid) : std::vector<EnemyFacts const*>();
        float gapHere = 1000.0f;
        for (EnemyFacts const* e : turnable)
            gapHere = std::min(gapHere, Exposure::Gap(*e, here));
        bool const danger = gapHere < DANGER_ENEMY_GAP;
        float const exposureHere = Exposure::LossAt(world, *self, turnable, here);
        if (!world.field || !world.field->sight)
            return;     // built while the party fights or a pull runs (PositionFieldBuilder)
        // Sight checks not kept yet, this tick (cells and the margin's points together).
        uint32 budget = MAX_SIGHT_CHECKS;
        // How narrowly a spot sees the victim (Perception/Values/SightMargin.h, 2026-11-14), in
        // the fight like the exposure.
        std::vector<Position3> const points =
            useExposure ? SightMargin::Points(world, at, atGuid, self->guid) : std::vector<Position3>();
        float const sightHere = inReach ? SightMargin::LossAt(world, *self, *world.field, points, here) : 0.0f;
        float const lossHere = exposureHere + sightHere;
        // The spot here is still good (header "When"): a cell must save more than the margin.
        bool const hereValid = !unsafe && (inReach || placed);
        float const margin = SpotCost::Walk(world, *self, MOVE_MARGIN_SEC);
        if (hereValid && lossHere <= margin)
        {
            // The area phase holds its caster out of the victim's reach: say so (the stance
            // leaves it there, the area phase moves it).
            if (placed && !inReach && !prepare && Trace::On())
                Trace::Emit("stance", Trace::Format("%s: victim out of reach, the area phase places it (ranged)", me->GetName()),
                    Trace::Format("{\"bot\":\"%s\",\"active\":%s}", Trace::Guid(me->GetObjectGuid()).c_str(),
                                  plan.area.CastBy(self->guid) ? "true" : "false"));
            return;
        }

        // The spot search (RangedSpot.h, the order of the header's "Where").
        RangedSpot::Query q;
        q.at = at;
        // Cells from the victim's edge: beyond the dead zone (DEAD_ZONE_MARGIN), up to MAX_RADIUS.
        q.minYd = deadZone > 0.0f ? deadZone + DEAD_ZONE_MARGIN + atReach : 0.0f;
        q.maxYd = (prepare ? PREPARE_MAX_RADIUS : MAX_RADIUS) + atReach;
        q.prepare = prepare;
        // (how long we stay: before the fight the pull's estimate, RouteGuard.h)
        q.staySec = RouteGuard::StaySec(world, plan);
        q.turnable = &turnable;
        q.points = &points;
        q.unsafe = unsafe;
        // The hard line (SafeRoute::TIGHT_MARGIN): in the fight, the victim out of range or sight
        // and no cell on the preferred line (stockade-20261114-203133: the mage stood in the
        // corridor while the tank fought in a cell, the opposite cell's margin closed its door).
        // A spot taken on the hard line is in the margin (`unsafe` next tick): the walk out goes
        // only to a cell that still sees the victim, so it does not walk back out.
        q.mayTighten = !prepare && !inReach && !placed;
        q.hereValid = hereValid;
        q.lossHere = lossHere;
        q.margin = margin;
        RangedSpot::Result found;
        if (RangedSpot::Find(me, world, *self, idle, q, budget, found))
        {
            RangedSpot::Spot const& s = found.spot;
            float const walkSec = s.walkSec;
            Proposal p;
            p.source = "rangedstance";
            p.action.type = BotActionType::MoveTo;
            p.action.x = s.pos.x;
            p.action.y = s.pos.y;
            p.action.z = s.pos.z;
            p.action.holdMs = uint32(walkSec * 1000.0f) + HOLD_EXTRA_MS;
            if (unsafe || danger)
            {
                // A constraint: never stand where an idle pack would come, nor in the enemies'
                // melee (their cleaves and area spells, an instant hit when one turns).
                p.tier = ProposalTier::Urgent;
                p.reason = unsafe ? "an idle creature would attack here" : "standing in the enemies' melee";
                p.score = -walkSec;
            }
            else if (prepare)
            {
                // Nothing to do before the fight: the walk costs nothing now and saves walking
                // it once the fight is on (its part of the planned fight, FightLoad estimate).
                float const incoming = std::max(world.rates.incomingDps, plan.pull.expectedIncomingDps);
                p.tier = ProposalTier::Assigned;
                p.reason = "take a place for the planned fight";
                p.SetValue(Value::ContributionPerSecAt(world, *self, incoming) * walkSec, 0.0f);
            }
            else
            {
                p.tier = ProposalTier::Assigned;
                p.reason = inDeadZone ? "inside the minimum range of its attack" :
                           !inReach ? (found.tight ? "victim out of range or sight (spot on the hard line)" : "victim out of range or sight") :
                           sightHere - s.sight > exposureHere - s.exposure ? "sees the victim only narrowly" : "too close to the enemies";
                // Out of reach: it cannot do its job for the rest of the fight (SpotCost::OutOfReach).
                // Plus the exposure and sight margin the walk saves.
                float benefit = inReach ? 0.0f : SpotCost::OutOfReach(world, *self);
                benefit += std::max(0.0f, lossHere - s.exposure - s.sight);
                p.SetValue(benefit, SpotCost::Walk(world, *self, walkSec));
            }
            out.push_back(p);
            return;
        }
        // The last way in: past the idle creature that blocked the first path, when its join is
        // worth it (JoinGamble.h, 2026-11-15).
        if (!prepare && !inReach && !placed && JoinGamble::Propose(me, world, *self, found.why, at, "rangedstance", out))
            return;
        // Out of the victim's range or sight and no spot: the only record of why it stands.
        if (!prepare && !inReach && Trace::On())
            Trace::Emit("stance", Trace::Format("%s: no spot for %s (ranged)", me->GetName(), q.mayTighten ? "the victim, hard line too" : "the victim"),
                Trace::Format("{\"bot\":\"%s\",\"cells\":%u,\"checked\":%u,\"tight\":%s,%s}",
                    Trace::Guid(me->GetObjectGuid()).c_str(), uint32(found.cells), found.checked, q.mayTighten ? "true" : "false",
                    found.why.Json().c_str()));
    }
}
