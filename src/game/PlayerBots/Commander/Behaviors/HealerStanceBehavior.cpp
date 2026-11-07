#include "Commander/Behaviors/HealerStanceBehavior.h"
#include "Commander/Perception/SafeRoute.h"
#include "Commander/Perception/HazardZones.h"
#include "Commander/Perception/ValueModel.h"
#include "Commander/Perception/Tanks.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Coordination/PatrolAssigner.h"     // InPath: pure, facts only
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

        float const STRICT_RADII[] = { 16.0f, 20.0f, 12.0f };
        // Second pass: closer, or further out (still inside HEAL_RANGE), with the smaller gap.
        float const RELAXED_RADII[] = { 14.0f, 24.0f, 10.0f };
        // Before the fight only (Context::prepare), last: just inside HEAL_RANGE.
        float const PREPARE_OUTER_RADII[] = { 27.0f, 26.0f };

        struct Context
        {
            Player* me = nullptr;
            Map* map = nullptr;
            PartyMemberFacts const* self = nullptr;
            // Who to stay with (FindAnchor), or the planned fight spot (anchorGuid empty).
            Position3 anchor;
            ObjectGuid anchorGuid;
            // Before the fight (PullPlan::PlannedFightSpot): the enemies come from comeFrom.
            bool prepare = false;
            Position3 comeFrom;
            float expectedIncomingDps = 0.0f;   // PullPlan::expectedIncomingDps
            Position3 here;
            // Enemies that can act and are not hitting the healer.
            std::vector<EnemyFacts const*> enemies;
        };

        struct Spot
        {
            Position3 pos;
            float walk = 0.0f;
            float score = 0.0f;
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

        // An idle patrol's predicted walk (WorldModel::patrols, filled while the party fights)
        // comes within its aggro range + margin of `p` before the fight should end.
        bool InPatrolPath(WorldModel const& world, Position3 const& p)
        {
            float const remaining = Value::FightRemainingSec(world);
            for (PatrolFacts const& patrol : world.patrols)
                if (patrol.aggressive && !patrol.noAggro && !patrol.tappedByOther &&
                    PatrolAssigner::InPath(patrol, p, remaining))
                    return true;
            return false;
        }

        // Straight behind the anchor as seen from the enemies near it; no enemies: towards the healer.
        float BehindAngle(Context const& ctx)
        {
            Position3 const& a = ctx.anchor;
            // Before the fight: away from where the enemies come from; when they are fought
            // where they stand (the anchor is the pack), towards the healer (the party's side).
            if (ctx.prepare && Distance2d(a, ctx.comeFrom) >= 1.0f)
                return std::atan2(a.y - ctx.comeFrom.y, a.x - ctx.comeFrom.x);
            if (ctx.prepare)
                return std::atan2(ctx.here.y - a.y, ctx.here.x - a.x);
            float sx = 0.0f, sy = 0.0f;
            uint32 n = 0;
            for (EnemyFacts const* e : ctx.enemies)
            {
                if (Distance2d(a, e->pos) > HS::ENEMY_SCAN)
                    continue;
                sx += e->pos.x;
                sy += e->pos.y;
                ++n;
            }
            if (n)
                return std::atan2(a.y - sy / n, a.x - sx / n);
            return std::atan2(ctx.here.y - a.y, ctx.here.x - a.x);
        }

        // Ring candidates passing the cheap checks (no ground or path yet), best first.
        std::vector<Spot> Candidates(Context const& ctx, WorldModel const& world, std::vector<HazardZone> const& zones,
                                     SafeRoute::IdleCreatures const& idle, float const* radii, size_t radiusCount,
                                     float minGap)
        {
            std::vector<Spot> spots;
            Position3 const& a = ctx.anchor;
            float const behind = BehindAngle(ctx);
            for (size_t r = 0; r < radiusCount; ++r)
            {
                for (uint32 k = 0; k < HS::RING_POINTS; ++k)
                {
                    // 0, +1, -1, +2, -2 ... steps away from straight behind.
                    int32 const step = int32((k + 1) / 2) * ((k % 2) ? 1 : -1);
                    float const angle = behind + 2.0f * float(M_PI) * step / HS::RING_POINTS;
                    Spot s;
                    s.pos.x = a.x + radii[r] * std::cos(angle);
                    s.pos.y = a.y + radii[r] * std::sin(angle);
                    s.pos.z = a.z;
                    float const gap = MinGap(ctx, s.pos);
                    if (gap < minGap)
                        continue;
                    if (HazardZones::IsInside(zones, HAZARD_HEALER, s.pos) || idle.Near(s.pos) ||
                        InPatrolPath(world, s.pos))
                        continue;
                    // Other members in heal range of the point; other tanks count TANK_COVER_MULT times.
                    float covered = 0.0f;
                    for (PartyMemberFacts const& m : world.party)
                        if (!m.isPet && m.alive && m.guid != ctx.self->guid && m.guid != ctx.anchorGuid &&
                            Distance2d(s.pos, m.pos) <= HS::HEAL_RANGE)
                            covered += Tanks::IsTank(m) ? HS::TANK_COVER_MULT : 1.0f;
                    s.walk = Distance2d(ctx.here, s.pos);
                    s.score = -s.walk + HS::COVER_WEIGHT * covered + HS::GAP_WEIGHT * std::min(gap, HS::GAP_CAP);
                    spots.push_back(s);
                }
            }
            std::stable_sort(spots.begin(), spots.end(), [](Spot const& x, Spot const& y) { return x.score > y.score; });
            return spots;
        }

        // Map checks: ground near the anchor's height, line of sight to the anchor, safe path.
        // `pathIdle`: `idle`, or its LeavingFrom(here) copy when the healer stands in reach now.
        bool GeometryOk(Context const& ctx, SafeRoute::IdleCreatures const& idle,
                        SafeRoute::IdleCreatures const& pathIdle, Spot& s)
        {
            Position3 const& a = ctx.anchor;
            float const ground = ctx.map->GetHeight(s.pos.x, s.pos.y, a.z + 3.0f);
            if (ground <= INVALID_HEIGHT || std::fabs(ground - a.z) > HS::MAX_STEP_Z)
                return false;
            s.pos.z = ground;
            if (idle.Near(s.pos) || !Los(ctx.map, s.pos, a))
                return false;
            return SafeRoute::PathIsSafe(ctx.me, pathIdle, s.pos, HS::MAX_PATH_DETOUR);
        }

        // Standing where an idle creature would attack: a constraint, not a trade (same as the
        // "rangedstance" module). Urgent beats the routine heal orders (Assigned), an emergency
        // heal order (Urgent, score > 0) still goes first.
        void MakeUnsafeProposal(Context const& ctx, Position3 const& to, Proposal& p)
        {
            float const speed = std::max(1.0f, ctx.me->GetSpeed(MOVE_RUN));
            float const walkSec = Distance2d(ctx.here, to) * HS::PATH_FACTOR / speed;
            p.tier = ProposalTier::Urgent;
            p.source = "stance";
            p.reason = "an idle creature would attack here";
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
            for (EnemyFacts const* e : ctx.enemies)
            {
                float const gapNow = std::max(0.0f, Distance2d(ctx.here, e->pos) - e->boundingRadius);
                if (gapNow >= HS::MIN_ENEMY_GAP)
                    continue;
                float const gapThere = std::max(0.0f, Distance2d(to, e->pos) - e->boundingRadius);
                float const gain = std::min(HS::MAX_GAIN_SEC, std::max(0.0f, gapThere - gapNow) / HS::ENEMY_RUN_SPEED);
                if (gain <= 0.0f)
                    continue;
                benefit += HS::TURN_CHANCE * (Value::HitLoss(world, *e, self, gain) + Value::LostAction(world, self, gain));
            }
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
            Proposal hold;
            hold.tier = unsafe ? ProposalTier::Urgent : ProposalTier::Assigned;
            hold.score = 0.0f;
            hold.source = "stance";
            hold.reason = unsafe ? "walking out of an idle creature's reach, keep walking"
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
            ctx.comeFrom = plan.pull.packAt;
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
        for (EnemyFacts const& e : world.enemies)
            if (e.acting && !e.onAllyOnly && e.victim != ctx.self->guid)
                ctx.enemies.push_back(&e);

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
        bool const crowded = MinGap(ctx, ctx.here) < TRIGGER_ENEMY_GAP;
        if (anchorOk && !crowded && !unsafe)
            return;
        char const* const reason = ctx.prepare ? "take a place for the planned fight" :
                                   anchorOk ? "too close to the enemies" : "tank out of reach";

        std::vector<HazardZone> const& zones = HazardZones::Get(world);
        // The walk out of an idle creature's reach starts inside it.
        SafeRoute::IdleCreatures const pathIdle = unsafe ? idle.LeavingFrom(here) : idle;

        uint32 checks = 0;
        // Before the fight a third pass at the edge of heal range: a walk-in fought where the
        // pack stands has the idle pack itself as the anchor, the inner rings lie in its aggro range.
        uint32 const passes = ctx.prepare ? 3 : 2;
        for (uint32 pass = 0; pass < passes; ++pass)
        {
            std::vector<Spot> spots = pass == 0
                ? Candidates(ctx, world, zones, idle, STRICT_RADII, sizeof(STRICT_RADII) / sizeof(float), MIN_ENEMY_GAP)
                : pass == 1
                ? Candidates(ctx, world, zones, idle, RELAXED_RADII, sizeof(RELAXED_RADII) / sizeof(float), RELAXED_ENEMY_GAP)
                : Candidates(ctx, world, zones, idle, PREPARE_OUTER_RADII, sizeof(PREPARE_OUTER_RADII) / sizeof(float),
                             RELAXED_ENEMY_GAP);
            for (Spot& s : spots)
            {
                if (checks >= MAX_GEOMETRY_CHECKS)
                    break;
                ++checks;
                if (!GeometryOk(ctx, idle, pathIdle, s))
                    continue;
                Proposal p;
                if (unsafe)
                    MakeUnsafeProposal(ctx, s.pos, p);
                else
                    MakeProposal(ctx, world, s.pos, anchorOk, anyInjured, reason, p);
                out.push_back(p);
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
                MakeUnsafeProposal(ctx, ctx.anchor, p);
            else
                MakeProposal(ctx, world, ctx.anchor, anchorOk, anyInjured,
                             ctx.prepare ? "planned fight spot, no free spot around it: go there"
                                         : "tank out of reach, no free spot: go to the tank", p);
            out.push_back(p);
        }
    }
}
