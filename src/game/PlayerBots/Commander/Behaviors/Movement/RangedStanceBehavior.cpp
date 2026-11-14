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
#include "Commander/Perception/Spatial/SightCache.h"
#include "Commander/Perception/Values/SightMargin.h"
#include "Commander/Behaviors/Movement/SpotRejects.h"
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

        struct Spot
        {
            Position3 pos;
            float walkSec = 0.0f;
            float exposure = 0.0f;  // points: Exposure::LossAt there
            float sight = 0.0f;     // points: SightMargin::LossAt there (cells that see the victim)
            float loss = 0.0f;      // points: SpotCost::Walk + exposure (+ sight), lower first
            bool taken = false;     // FieldSpots::Spot::taken
        };

        void SortSpots(WorldModel const& world, PartyMemberFacts const& self, float speed, std::vector<Spot>& spots)
        {
            std::stable_sort(spots.begin(), spots.end(), [](Spot const& a, Spot const& b) { return a.loss < b.loss; });
            // Equally good: within one cell of walk.
            float const tie = SpotCost::Walk(world, self, PositionField::CELL_YD * RS::PATH_FACTOR / speed);
            FieldSpots::PreferFree(spots, [](Spot const& s) { return s.loss; }, [](Spot const& s) { return s.taken; }, tie);
        }

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
            SortSpots(world, self, speed, spots);
            return spots;
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
        ObjectGuid atGuid;          // the victim (empty before the fight)
        float atReach = 0.0f;
        bool inReach = false;
        if (prepare)
        {
            at = planned;
            inReach = Distance2d(here, at) <= CAST_RANGE && SightCache::Live(me, here, at);
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
            atGuid = victim->GetObjectGuid();
            atReach = victim->GetCombatReach();
            // (the spell's line: SightCache::Live)
            inReach = me->GetCombatDistance(victim) <= CAST_RANGE && SightCache::Live(me, here, at);
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
        if (!world.field || !world.field->sight)
            return;     // built while the party fights or a pull runs (PositionFieldBuilder)
        SightCache& sight = *world.field->sight;
        // Sight checks not kept yet, this tick (cells and the margin's points together).
        uint32 budget = MAX_SIGHT_CHECKS;
        // How narrowly a spot sees the victim (Perception/Values/SightMargin.h, 2026-11-14), in
        // the fight like the exposure.
        std::vector<Position3> const points =
            useExposure ? SightMargin::Points(world, at, atGuid, self->guid) : std::vector<Position3>();
        float const sightHere = inReach ? SightMargin::LossAt(world, *self, me, sight, points, here, budget) : 0.0f;
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
        SpotRejects why;
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
            // Cells that see the victim, with their sight margin (header "Where"). Sorted by the
            // loss without the margin, which only adds: past the best full loss so far, or past
            // what saves the margin over here, nothing further down can win.
            std::vector<Spot> seen;
            float bestLoss = 1e9f;
            for (Spot const& s : spots)
            {
                if (s.loss >= bestLoss || (hereValid && lossHere - s.loss <= margin))
                    break;
                if (own.Near(s.pos))
                {
                    ++why.idle;
                    continue;
                }
                ++checked;
                SightCache::Sight const v = sight.Cells(me, s.pos, at, budget);
                if (v == SightCache::Sight::Unknown)
                {
                    why.budget = true;
                    break;      // out of budget: the next ticks go on (the answers are kept)
                }
                if (v == SightCache::Sight::No)
                {
                    ++why.hidden;
                    continue;
                }
                Spot full = s;
                full.sight = SightMargin::LossAt(world, *self, me, sight, points, s.pos, budget);
                full.loss += full.sight;
                bestLoss = std::min(bestLoss, full.loss);
                seen.push_back(full);
            }
            SortSpots(world, *self, speed, seen);
            why.seen += uint32(seen.size());
            // The path only on cells that passed the sight check (stockade-20261114-221943); a
            // cell that failed it is skipped for a while (SightCache::PathFailed), so the next
            // tick checks the next ones instead of the same MAX_GEOMETRY_CHECKS again
            // (stockade-20261115-000917: mage and priest stood in the corridor for 30 s).
            uint32 checks = 0;
            for (Spot const& s : seen)
            {
                if (hereValid && lossHere - s.loss <= margin)
                    return;
                if (sight.PathFailed(me, s.pos, tight))
                {
                    ++why.pathKept;
                    continue;
                }
                if (checks >= MAX_GEOMETRY_CHECKS)
                    break;
                ++checks;
                // The kept answer is from the cells' centres: the spell's line now, then the path.
                if (!SightCache::Live(me, s.pos, at))
                {
                    ++why.live;
                    continue;
                }
                if (!SafeRoute::PathIsSafe(me, pathIdle, s.pos, MAX_PATH_DETOUR))
                {
                    ++why.path;
                    sight.MarkPathFailed(me, s.pos, tight);
                    continue;
                }

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
                    p.reason = !inReach ? (tight ? "victim out of range or sight (spot on the hard line)" : "victim out of range or sight") :
                               sightHere - s.sight > exposureHere - s.exposure ? "sees the victim only narrowly" : "too close to the enemies";
                    // Out of reach: it cannot do its job. Plus the exposure and sight margin the walk saves.
                    float benefit = inReach ? 0.0f : Value::LostAction(world, *self, horizon);
                    benefit += std::max(0.0f, lossHere - s.exposure - s.sight);
                    p.SetValue(benefit, SpotCost::Walk(world, *self, walkSec));
                }
                out.push_back(p);
                return;
            }
        }
        // Out of the victim's range or sight and no spot: the only record of why it stands.
        if (!prepare && !inReach && Trace::On())
            Trace::Emit("stance", Trace::Format("%s: no spot for %s (ranged)", me->GetName(), mayTighten ? "the victim, hard line too" : "the victim"),
                Trace::Format("{\"bot\":\"%s\",\"cells\":%u,\"checked\":%u,\"tight\":%s,%s}",
                    Trace::Guid(me->GetObjectGuid()).c_str(), uint32(found), checked, mayTighten ? "true" : "false",
                    why.Json().c_str()));
    }
}
