#include "Commander/Behaviors/HealerStanceBehavior.h"
#include "Commander/Behaviors/SafeRoute.h"
#include "Commander/Perception/HazardZones.h"
#include "Commander/Perception/ValueModel.h"
#include "Commander/Perception/Tanks.h"
#include "Commander/Coordination/TeamPlan.h"
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
        float const RELAXED_RADII[] = { 10.0f, 14.0f };

        struct Context
        {
            Player* me = nullptr;
            Map* map = nullptr;
            PartyMemberFacts const* self = nullptr;
            PartyMemberFacts const* anchor = nullptr;
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

        float MinGap(Context const& ctx, Position3 const& p)
        {
            float gap = 1000.0f;
            for (EnemyFacts const* e : ctx.enemies)
                gap = std::min(gap, Distance2d(p, e->pos));
            return gap;
        }

        // Straight behind the anchor as seen from the enemies near it; no enemies: towards the healer.
        float BehindAngle(Context const& ctx)
        {
            Position3 const& a = ctx.anchor->pos;
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

        // Ring candidates passing the distance-only checks, best first.
        std::vector<Spot> Candidates(Context const& ctx, WorldModel const& world, std::vector<HazardZone> const& zones,
                                     SafeRoute::IdleCreatures const& idle, float const* radii, size_t radiusCount,
                                     float minGap)
        {
            std::vector<Spot> spots;
            Position3 const& a = ctx.anchor->pos;
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
                    if (HazardZones::IsInside(zones, HAZARD_HEALER, s.pos) || idle.Near(s.pos))
                        continue;
                    // Other members in heal range of the point; other tanks count TANK_COVER_MULT times.
                    float covered = 0.0f;
                    for (PartyMemberFacts const& m : world.party)
                        if (!m.isPet && m.alive && m.guid != ctx.self->guid && m.guid != ctx.anchor->guid &&
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
        bool GeometryOk(Context const& ctx, SafeRoute::IdleCreatures const& idle, Spot& s)
        {
            Position3 const& a = ctx.anchor->pos;
            float const ground = ctx.map->GetHeight(s.pos.x, s.pos.y, a.z + 3.0f);
            if (ground <= INVALID_HEIGHT || std::fabs(ground - a.z) > HS::MAX_STEP_Z)
                return false;
            s.pos.z = ground;
            if (idle.Near(s.pos) || !Los(ctx.map, s.pos, a))
                return false;
            return SafeRoute::PathIsSafe(ctx.me, idle, s.pos, HS::MAX_PATH_DETOUR);
        }

        void MakeProposal(Context const& ctx, WorldModel const& world, Position3 const& to, bool anchorOk,
                          bool anyInjured, char const* reason, Proposal& p)
        {
            PartyMemberFacts const& self = *ctx.self;
            float const speed = std::max(1.0f, ctx.me->GetSpeed(MOVE_RUN));
            float const walkSec = Distance2d(ctx.here, to) * HS::PATH_FACTOR / speed;
            float const horizon = std::max(1.0f, std::min(HS::HORIZON_SEC, Value::FightRemainingSec(world)));

            float benefit = anchorOk ? 0.0f : Value::LostAction(world, self, horizon);
            for (EnemyFacts const* e : ctx.enemies)
            {
                float const gapNow = Distance2d(ctx.here, e->pos);
                if (gapNow >= HS::MIN_ENEMY_GAP)
                    continue;
                float const gain = std::min(HS::MAX_GAIN_SEC,
                    std::max(0.0f, Distance2d(to, e->pos) - gapNow) / HS::ENEMY_RUN_SPEED);
                if (gain <= 0.0f)
                    continue;
                benefit += HS::TURN_CHANCE * (Value::HitLoss(world, *e, self, gain) + Value::LostAction(world, self, gain));
            }
            float cost = Value::LostAction(world, self, walkSec);
            if (!anyInjured)
                cost *= HS::IDLE_COST_FACTOR;

            p.tier = ProposalTier::Assigned;
            p.source = "stance";
            p.reason = reason;
            p.action.type = BotActionType::MoveTo;
            p.action.x = to.x;
            p.action.y = to.y;
            p.action.z = to.z;
            p.action.holdMs = uint32(walkSec * 1000.0f) + HS::HOLD_EXTRA_MS;
            p.SetValue(benefit, cost);
        }
    }

    bool HealerStanceBehavior::AppliesTo(PartyBotAI const* bot) const
    {
        return bot->GetRole() == ROLE_HEALER;
    }

    void HealerStanceBehavior::Propose(PartyBotAI const* bot, WorldModel const& world,
                                       TeamPlan const& plan, std::vector<Proposal>& out) const
    {
        // The retreat owns movement (Override tier); the pull owns it while the tank goes out
        // and brings the pack back.
        if (plan.retreat.active)
            return;
        if (plan.pull.phase == PullPhase::Approach || plan.pull.phase == PullPhase::Return)
            return;

        Player* me = bot->me;
        if (!me->IsInCombat() || me->HasUnitState(UNIT_STATE_ROOT))
            return;

        // A MoveTo (this module's or another's) is under way: keep walking. Without a proposal
        // the rotation's first cast-time heal stops the walk (DoCastSpell -> StopMoving).
        if (bot->IsMovementHeld() && me->IsMoving())
        {
            Proposal hold;
            hold.tier = ProposalTier::Assigned;
            hold.score = 0.0f;
            hold.source = "stance";
            hold.reason = "walking to a healing spot, keep walking";
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
        ctx.anchor = FindAnchor(world, me->GetObjectGuid());
        if (!ctx.anchor)
            return;
        ctx.here.x = me->GetPositionX();
        ctx.here.y = me->GetPositionY();
        ctx.here.z = me->GetPositionZ();
        for (EnemyFacts const& e : world.enemies)
            if (e.acting && !e.onAllyOnly && e.victim != ctx.self->guid)
                ctx.enemies.push_back(&e);

        // Someone is about to die and can be healed from here: heal first.
        bool anyInjured = false;
        for (PartyMemberFacts const& m : world.party)
        {
            if (m.isPet || !m.alive)
                continue;
            if (m.healthPct < INJURED_PCT)
                anyInjured = true;
            if (m.healthPct < URGENT_HEALTH_PCT && Distance2d(ctx.here, m.pos) <= HEAL_RANGE &&
                (m.guid == ctx.self->guid || Los(ctx.map, ctx.here, m.pos)))
                return;
        }

        bool const anchorOk = Distance2d(ctx.here, ctx.anchor->pos) <= HEAL_RANGE && Los(ctx.map, ctx.here, ctx.anchor->pos);
        bool const crowded = MinGap(ctx, ctx.here) < TRIGGER_ENEMY_GAP;
        if (anchorOk && !crowded)
            return;
        char const* const reason = anchorOk ? "too close to the enemies" : "tank out of reach";

        std::vector<HazardZone> const& zones = HazardZones::Get(world);
        SafeRoute::IdleCreatures const idle(me);

        uint32 checks = 0;
        for (uint32 pass = 0; pass < 2; ++pass)
        {
            std::vector<Spot> spots = pass == 0
                ? Candidates(ctx, world, zones, idle, STRICT_RADII, sizeof(STRICT_RADII) / sizeof(float), MIN_ENEMY_GAP)
                : Candidates(ctx, world, zones, idle, RELAXED_RADII, sizeof(RELAXED_RADII) / sizeof(float), RELAXED_ENEMY_GAP);
            for (Spot& s : spots)
            {
                if (checks >= MAX_GEOMETRY_CHECKS)
                    break;
                ++checks;
                if (!GeometryOk(ctx, idle, s))
                    continue;
                Proposal p;
                MakeProposal(ctx, world, s.pos, anchorOk, anyInjured, reason, p);
                out.push_back(p);
                return;
            }
        }

        // No spot: walk to the anchor itself while it is out of reach (what following did),
        // unless that walk passes an idle pack.
        if (!anchorOk && SafeRoute::PathIsSafe(me, idle, ctx.anchor->pos, MAX_PATH_DETOUR))
        {
            Proposal p;
            MakeProposal(ctx, world, ctx.anchor->pos, anchorOk, anyInjured, "tank out of reach, no free spot: go to the tank", p);
            out.push_back(p);
        }
    }
}
