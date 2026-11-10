#include "Commander/Behaviors/Movement/HealerStanceBehavior.h"
#include "Commander/Perception/Spatial/SafeRoute.h"
#include "Commander/Perception/Spatial/HazardZones.h"
#include "Commander/Perception/Values/ValueModel.h"
#include "Commander/Perception/Values/Exposure.h"
#include "Commander/Perception/Tanks.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Coordination/Situation/Patrol/RouteGuard.h"
#include "Commander/Perception/Spatial/PositionField.h"
#include "Commander/Perception/Spatial/FieldSpots.h"
#include "PartyBotAI.h"
#include "Map.h"
#include <algorithm>
#include <cmath>

namespace Commander
{
    namespace
    {
        using HS = HealerStanceBehavior;
        using SafeRoute::Distance2d;

        // Distance bands from the anchor (the rings before: 16, 20, 12; then 14, 24, 10; 27, 26).
        float const STRICT_MIN_YD = 12.0f;
        float const STRICT_MAX_YD = 20.0f;
        // Second pass: closer, or further out (still inside HEAL_RANGE), with the smaller gap.
        float const RELAXED_MIN_YD = 10.0f;
        float const RELAXED_MAX_YD = 24.0f;
        // Before the fight only (Context::prepare), last: just inside HEAL_RANGE.
        float const PREPARE_MAX_YD = 27.0f;

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
            float walk = 0.0f;
            float loss = 0.0f;      // points (SpotLoss), lower first
            bool taken = false;     // FieldSpots::Spot::taken
        };

        bool Los(Map* map, Position3 const& a, Position3 const& b)
        {
            return map->isInLineOfSight(a.x, a.y, a.z + HS::LOS_EYE_HEIGHT, b.x, b.y, b.z + HS::LOS_EYE_HEIGHT);
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
                gap = std::min(gap, std::max(0.0f, Distance2d(p, e->pos) - e->boundingRadius));
            return gap;
        }

        // Field cells `minR`..`maxR` from the anchor passing the cheap checks, best first. The
        // field drops cells off the ground, under an idle creature, on a patrol's way before the
        // fight ends and cells the healer cannot walk to (FieldSpots::Collect).
        // Points lost standing at a spot (header "Order"): the walk there, the walks to members
        // it does not reach weighted by their share of the damage taken, enemies close enough
        // to reach the healer before the tank takes them back.
        struct Weights
        {
            float speed = 7.0f;
            float walkFactor = 1.0f;            // IDLE_COST_FACTOR while nobody is injured
            // Other members (not the anchor, which every band reaches): share of the heals.
            std::vector<std::pair<PartyMemberFacts const*, float>> shares;
        };

        Weights MakeWeights(Context const& ctx, WorldModel const& world, bool anyInjured)
        {
            Weights w;
            w.speed = std::max(1.0f, ctx.me->GetSpeed(MOVE_RUN));
            w.walkFactor = anyInjured ? 1.0f : HS::IDLE_COST_FACTOR;
            float total = 0.0f;
            for (PartyMemberFacts const& m : world.party)
            {
                if (m.isPet || !m.alive || m.guid == ctx.self->guid || m.guid == ctx.anchorGuid)
                    continue;
                float const taken = Value::TakenDps(world, m.guid);
                w.shares.emplace_back(&m, taken);
                total += taken;
            }
            for (auto& s : w.shares)
                s.second = total > 0.0f ? s.second / total : 1.0f / float(w.shares.size());
            return w;
        }

        float SpotLoss(Context const& ctx, WorldModel const& world, Weights const& w, Position3 const& p, float walkYd)
        {
            PartyMemberFacts const& self = *ctx.self;
            float loss = w.walkFactor * Value::LostAction(world, self, walkYd / w.speed);
            for (auto const& s : w.shares)
            {
                float const out = Distance2d(p, s.first->pos) - HS::HEAL_RANGE;
                if (out > 0.0f)
                    loss += s.second * Value::LostAction(world, self, out * HS::PATH_FACTOR / w.speed);
            }
            return loss + Exposure::LossAt(world, self, ctx.enemies, p);
        }

        std::vector<Spot> Candidates(Context const& ctx, WorldModel const& world, std::vector<HazardZone> const& zones,
                                     Weights const& w, std::vector<FieldSpots::Spot> const& cells, float minR, float maxR,
                                     float minGap)
        {
            std::vector<Spot> spots;
            for (FieldSpots::Spot const& c : cells)
            {
                float const r = Distance2d(c.pos, ctx.anchor);
                if (r < minR || r > maxR)
                    continue;
                Spot s;
                s.pos = c.pos;
                if (MinGap(ctx, s.pos) < minGap)
                    continue;
                if (HazardZones::IsInside(zones, HAZARD_HEALER, s.pos))
                    continue;
                s.walk = c.WalkEstimate(HS::PATH_FACTOR);
                s.loss = SpotLoss(ctx, world, w, s.pos, s.walk);
                s.taken = c.taken;
                spots.push_back(s);
            }
            std::sort(spots.begin(), spots.end(), [](Spot const& x, Spot const& y) { return x.loss < y.loss; });
            // Equally good: within one cell of walk.
            float const tie = w.walkFactor * Value::LostAction(world, *ctx.self, PositionField::CELL_YD * HS::PATH_FACTOR / w.speed);
            FieldSpots::PreferFree(spots, [](Spot const& s) { return s.loss; }, [](Spot const& s) { return s.taken; }, tie);
            return spots;
        }

        // Live checks: this healer's own idle scan, line of sight to the anchor, safe path.
        // `pathIdle`: `idle`, or its LeavingFrom(here) copy when the healer stands in reach now.
        bool GeometryOk(Context const& ctx, SafeRoute::IdleCreatures const& idle,
                        SafeRoute::IdleCreatures const& pathIdle, Spot const& s)
        {
            Position3 const& a = ctx.anchor;
            if (idle.Near(s.pos) || !Los(ctx.map, s.pos, a))
                return false;
            return SafeRoute::PathIsSafe(ctx.me, pathIdle, s.pos, HS::MAX_PATH_DETOUR);
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

        void MakeProposal(Context const& ctx, WorldModel const& world, Position3 const& to, bool anchorOk,
                          bool anyInjured, char const* reason, Proposal& p)
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
            // The enemies' exposure saved (Perception/Values/Exposure.h, the same term as the order).
            benefit += std::max(0.0f, Exposure::LossAt(world, self, ctx.enemies, ctx.here) -
                                      Exposure::LossAt(world, self, ctx.enemies, to));
            float cost = Value::LostAction(world, self, walkSec);
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
        return what == Takeover::Stance && world.InFight(bot->me->IsInCombat()) && !bot->IsInDuel() &&
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
        bool const prepare = !inCombat && plan.pull.PlannedFightSpot(planned);
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
                (m.guid == ctx.self->guid || Los(ctx.map, ctx.here, m.pos)))
                return;
        }

        bool const anchorOk = Distance2d(ctx.here, ctx.anchor) <= HEAL_RANGE && Los(ctx.map, ctx.here, ctx.anchor);
        float const gapHere = MinGap(ctx, ctx.here);
        bool const crowded = gapHere < TRIGGER_ENEMY_GAP;
        if (anchorOk && !crowded && !unsafe)
            return;
        // In the enemies' melee (their cleaves and area spells, an instant hit when one turns):
        // a constraint like the idle reach, not a trade against the heal orders.
        bool const danger = !ctx.prepare && gapHere < DANGER_ENEMY_GAP;
        char const* const reason = ctx.prepare ? "take a place for the planned fight" :
                                   anchorOk ? "too close to the enemies" : "tank out of reach";
        auto const propose = [&](Position3 const& to)
        {
            Proposal p;
            if (unsafe)
                MakeUnsafeProposal(ctx, to, "an idle creature would attack here", p);
            else if (danger)
                MakeUnsafeProposal(ctx, to, "standing in the enemies' melee", p);
            else
                MakeProposal(ctx, world, to, anchorOk, anyInjured, reason, p);
            out.push_back(p);
        };

        std::vector<HazardZone> const& zones = HazardZones::Get(world);
        // The walk out of an idle creature's reach starts inside it.
        SafeRoute::IdleCreatures const pathIdle = unsafe ? idle.LeavingFrom(here) : idle;

        // Cells of the position field around the anchor, once for all passes.
        std::vector<FieldSpots::Spot> cells;
        if (world.field)
        {
            FieldSpots::Query q;
            q.center = ctx.anchor;
            q.minYd = RELAXED_MIN_YD;
            q.maxYd = ctx.prepare ? PREPARE_MAX_YD : RELAXED_MAX_YD;
            // (how long we stay: before the fight the pull's estimate, RouteGuard.h)
            q.patrolBeforeSec = RouteGuard::StaySec(world, plan);
            cells = FieldSpots::Collect(world, *ctx.self, q);
        }

        Weights const weights = MakeWeights(ctx, world, anyInjured);
        uint32 checks = 0;
        // Before the fight a third pass at the edge of heal range: a walk-in fought where the
        // pack stands has the idle pack itself as the anchor, the inner cells lie in its aggro range.
        uint32 const passes = ctx.prepare ? 3 : 2;
        for (uint32 pass = 0; pass < passes; ++pass)
        {
            std::vector<Spot> spots = pass == 0
                ? Candidates(ctx, world, zones, weights, cells, STRICT_MIN_YD, STRICT_MAX_YD, MIN_ENEMY_GAP)
                : pass == 1
                ? Candidates(ctx, world, zones, weights, cells, RELAXED_MIN_YD, RELAXED_MAX_YD, RELAXED_ENEMY_GAP)
                : Candidates(ctx, world, zones, weights, cells, RELAXED_MAX_YD, PREPARE_MAX_YD, RELAXED_ENEMY_GAP);
            for (Spot const& s : spots)
            {
                if (checks >= MAX_GEOMETRY_CHECKS)
                    break;
                ++checks;
                if (!GeometryOk(ctx, idle, pathIdle, s))
                    continue;
                propose(s.pos);
                return;
            }
        }

        // Too close with the tank in reach and no cell past the gap lines passed: any cell
        // farther from the enemies than here by IMPROVE_MIN_YD, best order first, own budget
        // (before: the healer stayed in the pile, 2026-11-10).
        if (crowded && anchorOk && !ctx.prepare)
        {
            std::vector<Spot> spots = Candidates(ctx, world, zones, weights, cells, RELAXED_MIN_YD, RELAXED_MAX_YD,
                                                 gapHere + IMPROVE_MIN_YD);
            uint32 tries = 0;
            for (Spot const& s : spots)
            {
                if (tries >= FALLBACK_GEOMETRY_CHECKS)
                    break;
                ++tries;
                if (!GeometryOk(ctx, idle, pathIdle, s))
                    continue;
                propose(s.pos);
                return;
            }
        }

        // No spot: walk to the anchor itself while it is out of reach (what following did) or
        // the healer stands in an idle creature's reach, unless the anchor stands in one too or
        // the walk passes an idle pack.
        if ((!anchorOk || unsafe) && !idle.Near(ctx.anchor) &&
            SafeRoute::PathIsSafe(me, pathIdle, ctx.anchor, MAX_PATH_DETOUR))
        {
            Proposal p;
            if (unsafe)
                MakeUnsafeProposal(ctx, ctx.anchor, "an idle creature would attack here", p);
            else
                MakeProposal(ctx, world, ctx.anchor, anchorOk, anyInjured,
                             ctx.prepare ? "planned fight spot, no free spot around it: go there"
                                         : "tank out of reach, no free spot: go to the tank", p);
            out.push_back(p);
        }
    }
}
