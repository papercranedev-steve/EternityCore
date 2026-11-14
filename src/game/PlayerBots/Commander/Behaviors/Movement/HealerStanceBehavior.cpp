#include "Commander/Behaviors/Movement/HealerStanceBehavior.h"
#include "Commander/Perception/Spatial/SafeRoute.h"
#include "Commander/Perception/Spatial/HazardZones.h"
#include "Commander/Perception/Values/ValueModel.h"
#include "Commander/Perception/Values/Exposure.h"
#include "Commander/Perception/Values/SpotCost.h"
#include "Commander/Perception/Values/StandLoss.h"
#include "Commander/Perception/Tanks.h"
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
        using HS = HealerStanceBehavior;
        using SafeRoute::Distance2d;


        struct Context
        {
            Player* me = nullptr;
            Map* map = nullptr;
            PartyMemberFacts const* self = nullptr;
            // Who to stay with (FindAnchor), or the planned fight spot (anchorGuid empty).
            Position3 anchor;
            ObjectGuid anchorGuid;
            // Before the fight (PullPlan::PlannedFightSpot).
            bool prepare = false;
            float expectedIncomingDps = 0.0f;   // PullPlan::expectedIncomingDps
            Position3 here;
            // Enemies that can act and are not hitting the healer.
            std::vector<EnemyFacts const*> enemies;
        };

        struct Spot
        {
            Position3 pos;
            float walkSec = 0.0f;
            float stay = 0.0f;      // points of standing there (StayLoss; + sight for cells that see the anchor)
            float sight = 0.0f;     // points: SightMargin::LossAt there
            float loss = 0.0f;      // points: the walk + stay, lower first
            bool taken = false;     // FieldSpots::Spot::taken
        };

        // (the heal's line: SightCache::Live, as Spell::CheckCast)
        bool Los(Player const* me, Position3 const& a, Position3 const& b)
        {
            return SightCache::Live(me, a, b);
        }

        Position3 PositionOf(WorldObject const* obj)
        {
            Position3 p;
            p.x = obj->GetPositionX();
            p.y = obj->GetPositionY();
            p.z = obj->GetPositionZ();
            return p;
        }

        uint32 HitsOn(WorldModel const& world, ObjectGuid guid)
        {
            uint32 hits = 0;
            for (EnemyFacts const& e : world.enemies)
                if (e.acting && e.victim == guid)
                    ++hits;
            return hits;
        }

        // A tank (Perception/Tanks.h; with several, the one most enemies hit, main tank on a
        // tie), else the member most enemies hit, else the leader.
        PartyMemberFacts const* FindAnchor(WorldModel const& world, ObjectGuid self)
        {
            PartyMemberFacts const* best = nullptr;
            uint32 bestHits = 0;
            for (PartyMemberFacts const* t : Tanks::Alive(world))
            {
                if (t->guid == self)
                    continue;
                uint32 const hits = HitsOn(world, t->guid);
                if (!best || hits > bestHits)
                {
                    best = t;
                    bestHits = hits;
                }
            }
            if (best)
                return best;
            for (PartyMemberFacts const& m : world.party)
            {
                if (m.isPet || !m.alive || m.guid == self)
                    continue;
                uint32 const hits = HitsOn(world, m.guid);
                if (hits > bestHits)
                {
                    bestHits = hits;
                    best = &m;
                }
            }
            if (best)
                return best;
            PartyMemberFacts const* leader = world.FindMember(world.leader);
            return (leader && leader->alive && leader->guid != self) ? leader : nullptr;
        }

        // Distance to the edge of the nearest enemy (a big boss reaches further than its centre).
        float MinGap(Context const& ctx, Position3 const& p)
        {
            float gap = 1000.0f;
            for (EnemyFacts const* e : ctx.enemies)
                gap = std::min(gap, Exposure::Gap(*e, p));
            return gap;
        }

        // What a spot costs (header "Order"): the walk there, and standing there: the walks to
        // members it does not reach weighted by their share of the damage taken, enemies close
        // enough to reach the healer before the tank takes them back (Perception/Values/
        // StandLoss.h since 2026-11-14: the fight spot prices the healer's place the same way).
        using Weights = StandLoss::HealerWeights;

        Weights MakeWeights(Context const& ctx, WorldModel const& world, bool anyInjured)
        {
            return StandLoss::HealerWeightsOf(world, *ctx.self, ctx.anchorGuid, ctx.me->GetSpeed(MOVE_RUN), anyInjured);
        }

        // Points of standing at `p` (the walk there not included).
        float StayLoss(Context const& ctx, WorldModel const& world, Weights const& w, Position3 const& p)
        {
            return StandLoss::HealerStay(world, *ctx.self, w, ctx.enemies, p);
        }

        void SortSpots(Context const& ctx, WorldModel const& world, Weights const& w, std::vector<Spot>& spots)
        {
            std::stable_sort(spots.begin(), spots.end(), [](Spot const& x, Spot const& y) { return x.loss < y.loss; });
            // Equally good: within one cell of walk.
            float const tie = w.walkFactor * SpotCost::Walk(world, *ctx.self, PositionField::CELL_YD * HS::PATH_FACTOR / w.speed);
            FieldSpots::PreferFree(spots, [](Spot const& s) { return s.loss; }, [](Spot const& s) { return s.taken; }, tie);
        }

        // Field cells passing the cheap checks (outside areas dangerous to healers), lowest
        // loss first. The field already dropped cells off the ground, under an idle creature,
        // on a patrol's way before the stay ends and cells the healer cannot walk to.
        std::vector<Spot> Candidates(Context const& ctx, WorldModel const& world, std::vector<HazardZone> const& zones,
                                     Weights const& w, std::vector<FieldSpots::Spot> const& cells)
        {
            std::vector<Spot> spots;
            for (FieldSpots::Spot const& c : cells)
            {
                if (HazardZones::IsInside(zones, HAZARD_HEALER, c.pos))
                    continue;
                Spot s;
                s.pos = c.pos;
                s.walkSec = c.WalkEstimate(HS::PATH_FACTOR) / w.speed;
                s.stay = StayLoss(ctx, world, w, s.pos);
                s.loss = w.walkFactor * SpotCost::Walk(world, *ctx.self, s.walkSec) + s.stay;
                s.taken = c.taken;
                spots.push_back(s);
            }
            SortSpots(ctx, world, w, spots);
            return spots;
        }

        // Standing where an idle creature would attack, or in the enemies' melee (DANGER_ENEMY_GAP):
        // a constraint, not a trade (same as the "rangedstance" module). Urgent beats the
        // routine heal orders (Assigned), an emergency heal order (Urgent, score > 0) still goes first.
        void MakeUnsafeProposal(Context const& ctx, Position3 const& to, char const* reason, Proposal& p)
        {
            float const speed = std::max(1.0f, ctx.me->GetSpeed(MOVE_RUN));
            float const walkSec = Distance2d(ctx.here, to) * HS::PATH_FACTOR / speed;
            p.tier = ProposalTier::Urgent;
            p.source = "stance";
            p.reason = reason;
            p.action.type = BotActionType::MoveTo;
            p.action.x = to.x;
            p.action.y = to.y;
            p.action.z = to.z;
            p.action.holdMs = uint32(walkSec * 1000.0f) + HS::HOLD_EXTRA_MS;
            p.score = -walkSec;
        }

        // `staySaved`: StayLoss here - there (the order's term), 0 for the walk to the anchor.
        void MakeProposal(Context const& ctx, WorldModel const& world, Position3 const& to, bool anchorOk,
                          bool anyInjured, float staySaved, char const* reason, Proposal& p)
        {
            PartyMemberFacts const& self = *ctx.self;
            float const speed = std::max(1.0f, ctx.me->GetSpeed(MOVE_RUN));
            float const walkSec = Distance2d(ctx.here, to) * HS::PATH_FACTOR / speed;
            p.tier = ProposalTier::Assigned;
            p.source = "stance";
            p.reason = reason;
            p.action.type = BotActionType::MoveTo;
            p.action.x = to.x;
            p.action.y = to.y;
            p.action.z = to.z;
            p.action.holdMs = uint32(walkSec * 1000.0f) + HS::HOLD_EXTRA_MS;
            if (ctx.prepare)
            {
                // Nothing to do before the fight: the walk costs nothing now, and saves walking
                // it once the fight is on (the healer's part of the planned fight's damage).
                float const incoming = std::max(world.rates.incomingDps, ctx.expectedIncomingDps);
                p.SetValue(Value::ContributionPerSecAt(world, self, incoming) * walkSec, 0.0f);
                return;
            }
            float const horizon = std::max(1.0f, std::min(HS::HORIZON_SEC, Value::FightRemainingSec(world)));

            float benefit = anchorOk ? 0.0f : Value::LostAction(world, self, horizon);
            // Members in reach and the enemies' exposure (the same terms as the order).
            benefit += std::max(0.0f, staySaved);
            float cost = SpotCost::Walk(world, self, walkSec);
            if (!anyInjured)
                cost *= HS::IDLE_COST_FACTOR;
            p.SetValue(benefit, cost);
        }
    }

    bool HealerStanceBehavior::AppliesTo(PartyBotAI const* bot) const
    {
        return bot->GetRole() == ROLE_HEALER;
    }

    bool HealerStanceBehavior::TakesOver(Takeover what, PartyBotAI const* bot, WorldModel const& world,
                                         TeamPlan const& /*plan*/) const
    {
        return what == Takeover::Stance && world.InFight(bot->me->IsInCombat()) &&
               !bot->me->GetTransport();
    }

    void HealerStanceBehavior::Propose(PartyBotAI const* bot, WorldModel const& world,
                                       TeamPlan const& plan, std::vector<Proposal>& out) const
    {
        // The retreat owns movement (Override tier).
        if (plan.retreat.active)
            return;
        Player* me = bot->me;
        if (me->HasUnitState(UNIT_STATE_ROOT))
            return;
        // Hiding behind the tank during a line of sight pull: the brain chose the spot (PullBehavior).
        if (plan.pull.phase == PullPhase::Settle && plan.pull.LosHiderOf(me->GetObjectGuid()))
            return;

        // Before the fight: out of combat while the pull runs, take a place for the fight the
        // brain planned (PullPlan::PlannedFightSpot), then wait there (PullBehavior's hold).
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
            if (!inCombat && plan.pull.HoldsWaiting(true))
                return;
        }

        Position3 const here = PositionOf(me);
        SafeRoute::IdleCreatures const idle(me);
        bool const unsafe = idle.Near(here);

        // A MoveTo (this module's or another's) is under way: keep walking. Without a proposal
        // the rotation's first cast-time heal stops the walk (DoCastSpell -> StopMoving).
        // Still in an idle creature's reach: Urgent, so a routine heal order does not stop
        // the walk out there.
        if (bot->IsMovementHeld() && me->IsMoving())
        {
            // Still in the enemies' melee (DANGER_ENEMY_GAP): Urgent too.
            bool inMelee = false;
            for (EnemyFacts const* e : Exposure::Turnable(world, me->GetObjectGuid()))
                if (Exposure::Gap(*e, here) < DANGER_ENEMY_GAP)
                    inMelee = true;
            Proposal hold;
            hold.tier = unsafe || inMelee ? ProposalTier::Urgent : ProposalTier::Assigned;
            hold.score = 0.0f;
            hold.source = "stance";
            hold.reason = unsafe ? "walking out of an idle creature's reach, keep walking"
                                 : inMelee ? "walking out of the enemies' melee, keep walking"
                                           : "walking to a healing spot, keep walking";
            hold.action.type = BotActionType::Hold;
            out.push_back(hold);
            return;
        }
        if (me->IsNonMeleeSpellCasted(false, false, true))
            return;

        Context ctx;
        ctx.me = me;
        ctx.map = me->GetMap();
        ctx.self = world.FindMember(me->GetObjectGuid());
        if (!ctx.self || !ctx.self->CanAct())
            return;
        if (prepare)
        {
            ctx.prepare = true;
            ctx.anchor = planned;
            ctx.expectedIncomingDps = plan.pull.expectedIncomingDps;
        }
        else
        {
            PartyMemberFacts const* anchor = FindAnchor(world, me->GetObjectGuid());
            if (!anchor)
                return;
            ctx.anchor = anchor->pos;
            ctx.anchorGuid = anchor->guid;
        }
        ctx.here = here;
        ctx.enemies = Exposure::Turnable(world, ctx.self->guid);

        // Someone is about to die and can be healed from here: heal first. Not when standing in
        // an idle creature's reach: the arbiter weighs the emergency heal order against the walk.
        bool anyInjured = false;
        for (PartyMemberFacts const& m : world.party)
        {
            if (m.isPet || !m.alive)
                continue;
            if (m.healthPct < INJURED_PCT)
                anyInjured = true;
            if (!unsafe && m.healthPct < URGENT_HEALTH_PCT && Distance2d(ctx.here, m.pos) <= HEAL_RANGE &&
                (m.guid == ctx.self->guid || Los(me, ctx.here, m.pos)))
                return;
        }

        bool const anchorOk = Distance2d(ctx.here, ctx.anchor) <= HEAL_RANGE && Los(me, ctx.here, ctx.anchor);
        // (the field and its sight cache: built while the party fights or a pull runs; without
        // them no cells, only the walk to the anchor below)
        SightCache* const sight = world.field ? world.field->sight.get() : nullptr;
        // Sight checks not kept yet, this tick (cells and the margin's points together).
        uint32 budget = MAX_SIGHT_CHECKS;
        // How narrowly a spot sees the tank among the enemies it holds (Perception/Values/
        // SightMargin.h, 2026-11-14). Not before the fight.
        std::vector<Position3> const points = ctx.prepare ? std::vector<Position3>() :
            SightMargin::Points(world, ctx.anchor, ctx.anchorGuid, ctx.self->guid);
        // The spot here is still good (header "When"): a cell must save more than the margin.
        bool const hereValid = anchorOk && !unsafe;
        Weights const weights = MakeWeights(ctx, world, anyInjured);
        float const sightHere = anchorOk && sight ?
            SightMargin::LossAt(world, *ctx.self, me, *sight, points, ctx.here, budget) : 0.0f;
        float const stayHere = StayLoss(ctx, world, weights, ctx.here) + sightHere;
        float const margin = SpotCost::Walk(world, *ctx.self, MOVE_MARGIN_SEC);
        // Before the fight a valid spot is kept: the others are still walking to their places.
        bool const keepValid = hereValid && (ctx.prepare || stayHere <= margin);
        if (keepValid)
            return;
        // In the enemies' melee (their cleaves and area spells, an instant hit when one turns):
        // the move is Urgent like the idle reach, not a trade against the heal orders.
        bool const danger = !ctx.prepare && MinGap(ctx, ctx.here) < DANGER_ENEMY_GAP;
        char const* const reason = ctx.prepare ? "take a place for the planned fight" :
                                   anchorOk ? "a better healing spot (enemies, members in reach)" : "tank out of reach";
        auto const propose = [&](Position3 const& to, float staySaved)
        {
            Proposal p;
            if (unsafe)
                MakeUnsafeProposal(ctx, to, "an idle creature would attack here", p);
            else if (danger)
                MakeUnsafeProposal(ctx, to, "standing in the enemies' melee", p);
            else
                MakeProposal(ctx, world, to, anchorOk, anyInjured, staySaved, reason, p);
            out.push_back(p);
        };

        std::vector<HazardZone> const& zones = HazardZones::Get(world);
        // (how long we stay: before the fight the pull's estimate, RouteGuard.h)
        float const staySec = RouteGuard::StaySec(world, plan);
        // The hard line (SafeRoute::TIGHT_MARGIN): in the fight, the tank out of reach or sight
        // and nothing on the preferred line (stockade-20261114-203133: the tank fought in a cell,
        // the opposite cell's margin closed its door, the priest never healed and the tank died).
        // A spot taken on the hard line is in the margin (`unsafe` next tick): the walk out goes
        // only to a cell that still sees the tank (SightOk) or to the tank, not back out.
        bool const mayTighten = !ctx.prepare && !anchorOk;
        uint32 checked = 0;
        size_t found = 0;
        SpotRejects why;
        char const* anchorWhy = "";
        std::unique_ptr<SafeRoute::IdleCreatures> idleTight;
        for (bool const tight : { false, true })
        {
            if (tight && !mayTighten)
                break;
            // This healer's own idle scan on the pass's line (scanned again only for the hard line).
            if (tight)
                idleTight.reset(new SafeRoute::IdleCreatures(me, nullptr, SafeRoute::TIGHT_MARGIN));
            SafeRoute::IdleCreatures const& own = tight ? *idleTight : idle;
            // The walk out of an idle creature's reach starts inside it.
            SafeRoute::IdleCreatures const pathIdle = unsafe ? own.LeavingFrom(here) : own;

            // Cells of the position field within MAX_YD of the anchor (PREPARE_MAX_YD before the
            // fight: a walk-in fought where the pack stands has the idle pack itself as the anchor,
            // the inner cells lie in its aggro range and the field drops them).
            std::vector<Spot> spots;
            if (world.field && sight)
            {
                FieldSpots::Query q;
                q.center = ctx.anchor;
                q.minYd = 0.0f;
                q.maxYd = ctx.prepare ? PREPARE_MAX_YD : MAX_YD;
                q.patrolBeforeSec = staySec;
                q.tight = tight;
                spots = Candidates(ctx, world, zones, weights, FieldSpots::Collect(world, *ctx.self, q));
            }
            found += spots.size();

            // Cells that see the anchor, with their sight margin (header "Where"; same search as
            // the ranged stance). Sorted by the loss without the margin, which only adds: past
            // the best full loss so far, or past what saves the margin over here, nothing
            // further down can win.
            std::vector<Spot> seen;
            float bestLoss = 1e9f;
            for (Spot const& s : spots)
            {
                if (s.loss >= bestLoss || (hereValid && stayHere - s.loss <= margin))
                    break;
                if (own.Near(s.pos))
                {
                    ++why.idle;
                    continue;
                }
                ++checked;
                SightCache::Sight const v = sight->Cells(me, s.pos, ctx.anchor, budget);
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
                full.sight = SightMargin::LossAt(world, *ctx.self, me, *sight, points, s.pos, budget);
                full.stay += full.sight;
                full.loss += full.sight;
                bestLoss = std::min(bestLoss, full.loss);
                seen.push_back(full);
            }
            SortSpots(ctx, world, weights, seen);
            why.seen += uint32(seen.size());
            // The path (`pathIdle`: `own`, or its LeavingFrom(here) copy) only on cells that passed
            // the sight check (stockade-20261114-221943); a cell that failed it is skipped for a
            // while (SightCache::PathFailed, stockade-20261115-000917).
            uint32 checks = 0;
            for (Spot const& s : seen)
            {
                if (hereValid && stayHere - s.loss <= margin)
                    return;
                if (sight->PathFailed(me, s.pos, tight))
                {
                    ++why.pathKept;
                    continue;
                }
                if (checks >= MAX_GEOMETRY_CHECKS)
                    break;
                ++checks;
                // The kept answer is from the cells' centres: the heal's line now, then the path.
                if (!Los(me, s.pos, ctx.anchor))
                {
                    ++why.live;
                    continue;
                }
                if (!SafeRoute::PathIsSafe(me, pathIdle, s.pos, MAX_PATH_DETOUR))
                {
                    ++why.path;
                    sight->MarkPathFailed(me, s.pos, tight);
                    continue;
                }
                propose(s.pos, stayHere - s.stay);
                if (tight)
                    out.back().reason = "tank out of reach, spot on the hard line";
                else if (anchorOk && !unsafe && !danger &&
                         sightHere - s.sight > (stayHere - sightHere) - (s.stay - s.sight))
                    out.back().reason = "a better healing spot (sees the tank only narrowly)";
                return;
            }

            // No spot: walk to the anchor itself while it is out of reach (what following did) or
            // the healer stands in an idle creature's reach, unless the anchor stands in one too or
            // the walk passes an idle pack.
            // In the margin only (the hard line holds there) with the tank in reach and sight, e.g.
            // a spot taken on the hard line: staying beats the walk into the enemies at the tank.
            bool const marginOnly = unsafe && anchorOk &&
                !SafeRoute::IdleCreatures(me, nullptr, SafeRoute::TIGHT_MARGIN).Near(here);
            bool const wantsAnchor = !anchorOk || (unsafe && !marginOnly);
            bool const anchorIdle = wantsAnchor && own.Near(ctx.anchor);
            if (anchorIdle)
                anchorWhy = tight ? "tank in idle reach (hard line)" : "tank in idle reach";
            bool const anchorPath = wantsAnchor && !anchorIdle && SafeRoute::PathIsSafe(me, pathIdle, ctx.anchor, MAX_PATH_DETOUR);
            if (wantsAnchor && !anchorIdle && !anchorPath)
                anchorWhy = tight ? "path to the tank unsafe (hard line)" : "path to the tank unsafe";
            if (anchorPath)
            {
                Proposal p;
                if (unsafe)
                    MakeUnsafeProposal(ctx, ctx.anchor, "an idle creature would attack here", p);
                else
                    MakeProposal(ctx, world, ctx.anchor, anchorOk, anyInjured, 0.0f,
                                 ctx.prepare ? "planned fight spot, no free spot around it: go there" :
                                 tight ? "tank out of reach, no free spot: go to the tank on the hard line"
                                       : "tank out of reach, no free spot: go to the tank", p);
                out.push_back(p);
                return;
            }
        }
        // The tank out of reach or sight and no way to it: the only record of why it stands.
        if (!ctx.prepare && !anchorOk && Trace::On())
            Trace::Emit("stance", Trace::Format("%s: no spot for the tank (healer)%s", me->GetName(), mayTighten ? ", hard line too" : ""),
                Trace::Format("{\"bot\":\"%s\",\"cells\":%u,\"checked\":%u,\"tight\":%s,%s,\"anchor\":\"%s\"}",
                    Trace::Guid(me->GetObjectGuid()).c_str(), uint32(found), checked, mayTighten ? "true" : "false",
                    why.Json().c_str(), anchorWhy));
    }
}
