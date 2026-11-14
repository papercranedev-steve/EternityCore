#include "Commander/Behaviors/Movement/RangedStanceBehavior.h"
#include "Commander/Perception/Spatial/SafeRoute.h"
#include "Commander/Perception/Spatial/HazardZones.h"
#include "Commander/Perception/Values/ValueModel.h"
#include "Commander/Perception/Values/Exposure.h"
#include "Commander/Perception/Values/SpotCost.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Coordination/Situation/Patrol/RouteGuard.h"
#include "Commander/Perception/Spatial/PositionField.h"
#include "Commander/Perception/Spatial/FieldSpots.h"
#include "Commander/Trace/Trace.h"
#include "PartyBotAI.h"
#include "Map.h"
#include <algorithm>
#include <cmath>
#include <memory>

namespace Commander
{
    namespace
    {
        using RS = RangedStanceBehavior;
        using SafeRoute::Distance2d;

        Position3 PositionOf(WorldObject const* obj)
        {
            Position3 p;
            p.x = obj->GetPositionX();
            p.y = obj->GetPositionY();
            p.z = obj->GetPositionZ();
            return p;
        }

        bool Los(Map* map, Position3 const& a, Position3 const& b)
        {
            return map->isInLineOfSight(a.x, a.y, a.z + RS::LOS_EYE_HEIGHT, b.x, b.y, b.z + RS::LOS_EYE_HEIGHT);
        }

        struct Spot
        {
            Position3 pos;
            float walkSec = 0.0f;
            float exposure = 0.0f;  // points: Exposure::LossAt there
            float loss = 0.0f;      // points: SpotCost::Walk + exposure, lower first
            bool taken = false;     // FieldSpots::Spot::taken
        };

        // Field cells up to MAX_RADIUS (PREPARE_MAX_RADIUS before the fight) from the victim's
        // edge, outside areas dangerous to ranged; lowest loss first (header "Where"). The field
        // drops cells off the ground, under an idle creature, on a patrol's way before `staySec`,
        // and cells the bot cannot walk to. `turnable` empty: the walk alone.
        std::vector<Spot> Candidates(WorldModel const& world, PartyMemberFacts const& self,
                                     std::vector<HazardZone> const& zones, Position3 const& victim,
                                     float victimReach, bool prepare, float staySec, float speed,
                                     std::vector<EnemyFacts const*> const& turnable, bool tight)
        {
            std::vector<Spot> spots;
            FieldSpots::Query q;
            q.center = victim;
            q.minYd = 0.0f;
            q.maxYd = (prepare ? RS::PREPARE_MAX_RADIUS : RS::MAX_RADIUS) + victimReach;
            q.patrolBeforeSec = staySec;
            q.tight = tight;
            for (FieldSpots::Spot const& c : FieldSpots::Collect(world, self, q))
            {
                if (HazardZones::IsInside(zones, HAZARD_RANGED, c.pos))
                    continue;
                Spot s;
                s.pos = c.pos;
                s.walkSec = c.WalkEstimate(RS::PATH_FACTOR) / speed;
                s.exposure = Exposure::LossAt(world, self, turnable, c.pos);
                s.loss = SpotCost::Walk(world, self, s.walkSec) + s.exposure;
                s.taken = c.taken;
                spots.push_back(s);
            }
            std::sort(spots.begin(), spots.end(), [](Spot const& a, Spot const& b) { return a.loss < b.loss; });
            // Equally good: within one cell of walk.
            float const tie = SpotCost::Walk(world, self, PositionField::CELL_YD * RS::PATH_FACTOR / speed);
            FieldSpots::PreferFree(spots, [](Spot const& s) { return s.loss; }, [](Spot const& s) { return s.taken; }, tie);
            return spots;
        }

        // Cheap live checks: not in this bot's own idle scan's reach, line of sight to the victim.
        bool SightOk(Map* map, SafeRoute::IdleCreatures const& idle, Position3 const& victim, Spot const& s)
        {
            return !idle.Near(s.pos) && Los(map, s.pos, victim);
        }
    }

    bool RangedStanceBehavior::AppliesTo(PartyBotAI const* bot) const
    {
        return bot->GetRole() == ROLE_RANGE_DPS;
    }

    bool RangedStanceBehavior::TakesOver(Takeover what, PartyBotAI const* bot, WorldModel const& world,
                                         TeamPlan const& /*plan*/) const
    {
        return what == Takeover::Stance && world.InFight(bot->me->IsInCombat()) &&
               !bot->me->GetTransport();
    }

    void RangedStanceBehavior::Propose(PartyBotAI const* bot, WorldModel const& world,
                                       TeamPlan const& plan, std::vector<Proposal>& out) const
    {
        // The retreat owns movement (Override tier).
        if (plan.retreat.active)
            return;
        Player* me = bot->me;
        if (me->HasUnitState(UNIT_STATE_ROOT) || !bot->UsesCasterChase())
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
        float atReach = 0.0f;
        bool inReach = false;
        if (prepare)
        {
            at = planned;
            inReach = Distance2d(here, at) <= CAST_RANGE && Los(me->GetMap(), here, at);
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
            // It comes for the bot: running from melee is the escape module's job.
            if (victim->GetVictim() == me)
                return;
            at = PositionOf(victim);
            atReach = victim->GetCombatReach();
            inReach = me->GetCombatDistance(victim) <= CAST_RANGE && Los(me->GetMap(), here, at);
        }

        SafeRoute::IdleCreatures const idle(me);
        bool const unsafe = idle.Near(here);
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
        // The spot here is still good (header "When"): a cell must save more than the margin.
        bool const hereValid = !unsafe && (inReach || placed);
        float const margin = SpotCost::Walk(world, *self, MOVE_MARGIN_SEC);
        if (hereValid && exposureHere <= margin)
            return;

        if (!world.field)
            return;     // built while the party fights or a pull runs (PositionFieldBuilder)
        std::vector<HazardZone> const& zones = HazardZones::Get(world);
        float const speed = std::max(1.0f, me->GetSpeed(MOVE_RUN));
        // (how long we stay: before the fight the pull's estimate, RouteGuard.h)
        float const staySec = RouteGuard::StaySec(world, plan);
        // The hard line (SafeRoute::TIGHT_MARGIN): in the fight, the victim out of range or sight
        // and no cell on the preferred line (stockade-20261114-203133: the mage stood in the
        // corridor while the tank fought in a cell, the opposite cell's margin closed its door).
        // A spot taken on the hard line is in the margin (`unsafe` next tick): the walk out goes
        // only to a cell that still sees the victim (GeometryOk), so it does not walk back out.
        bool const mayTighten = !prepare && !inReach && !placed;
        uint32 checked = 0;
        size_t found = 0;
        std::unique_ptr<SafeRoute::IdleCreatures> idleTight;
        for (bool const tight : { false, true })
        {
            if (tight && !mayTighten)
                break;
            std::vector<Spot> spots = Candidates(world, *self, zones, at, atReach, prepare, staySec, speed, turnable, tight);
            found += spots.size();
            // This bot's own idle scan on the pass's line (scanned again only for the hard line).
            if (tight)
                idleTight.reset(new SafeRoute::IdleCreatures(me, nullptr, SafeRoute::TIGHT_MARGIN));
            SafeRoute::IdleCreatures const& own = tight ? *idleTight : idle;
            // The walk out of an idle creature's reach starts inside it.
            SafeRoute::IdleCreatures const pathIdle = unsafe ? own.LeavingFrom(here) : own;
            // Two budgets (stockade-20261114-221943: the mage in the corridor, its 6 nearest cells
            // all behind the wall, the same 6 every tick, the doorway never checked): sight on
            // up to MAX_SIGHT_CHECKS cells, the path only on cells that passed it.
            uint32 checks = 0;
            uint32 sightChecks = 0;
            for (Spot const& s : spots)
            {
                // Sorted: no cell further down saves more than this one.
                if (hereValid && exposureHere - s.loss <= margin)
                    return;
                if (checks >= MAX_GEOMETRY_CHECKS || sightChecks >= MAX_SIGHT_CHECKS)
                    break;
                ++sightChecks;
                if (!SightOk(me->GetMap(), own, at, s))
                    continue;
                ++checks;
                if (!SafeRoute::PathIsSafe(me, pathIdle, s.pos, MAX_PATH_DETOUR))
                    continue;

                float const walkSec = s.walkSec;
                float const horizon = std::max(1.0f, std::min(HORIZON_SEC, Value::FightRemainingSec(world)));
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
                    p.reason = inReach ? "too close to the enemies" :
                               tight ? "victim out of range or sight (spot on the hard line)" : "victim out of range or sight";
                    // Out of reach: it cannot do its job. Plus the exposure the walk saves.
                    float benefit = inReach ? 0.0f : Value::LostAction(world, *self, horizon);
                    benefit += std::max(0.0f, exposureHere - s.exposure);
                    p.SetValue(benefit, SpotCost::Walk(world, *self, walkSec));
                }
                out.push_back(p);
                return;
            }
            checked += sightChecks;
        }
        // Out of the victim's range or sight and no spot: the only record of why it stands.
        if (!prepare && !inReach && Trace::On())
            Trace::Emit("stance", Trace::Format("%s: no spot for %s (ranged)", me->GetName(), mayTighten ? "the victim, hard line too" : "the victim"),
                Trace::Format("{\"bot\":\"%s\",\"cells\":%u,\"checked\":%u,\"tight\":%s}",
                    Trace::Guid(me->GetObjectGuid()).c_str(), uint32(found), checked, mayTighten ? "true" : "false"));
    }
}
