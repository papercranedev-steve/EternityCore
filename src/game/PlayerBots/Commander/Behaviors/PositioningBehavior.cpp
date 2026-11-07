#include "Commander/Behaviors/PositioningBehavior.h"
#include "Commander/Perception/SafeRoute.h"
#include "Commander/Perception/HazardZones.h"
#include "Commander/Perception/PackResolver.h"
#include "Commander/Perception/ValueModel.h"
#include "Commander/Perception/Tanks.h"
#include "Commander/Coordination/TeamPlan.h"
#include "PartyBotAI.h"
#include "Map.h"
#include "Timer.h"
#include <algorithm>
#include <cmath>

namespace Commander
{
    namespace
    {
        using PB = PositioningBehavior;

        // What the bot needs to keep while it moves, read once per tick.
        struct Context
        {
            Player* me = nullptr;
            Map* map = nullptr;
            PartyMemberFacts const* self = nullptr;
            uint8 roleBit = 0;
            Position3 here;
            bool hasHolder = false;     // healer: the member to stay in heal range of
            Position3 holder;
            Unit* victim = nullptr;     // ranged / melee: current target
            Position3 victimPos;
            float speed = 7.0f;
            SafeRoute::IdleCreatures const* idle = nullptr;    // creatures not fighting yet, around the bot
        };

        struct Loss
        {
            float points = 0.0f;
            float damage = 0.0f;        // health, for the death check
        };

        struct Spot
        {
            Position3 pos;
            float dist = 0.0f;
            bool keepsReach = false;    // melee: victim still in melee reach from here
        };

        bool Contains(HazardZone const& z, Position3 const& p, float margin = 0.0f)
        {
            return HazardZones::DistanceTo(z, p) <= z.radius - margin;
        }

        float HorizonSec(WorldModel const& world, HazardZone const& z)
        {
            // Copies: std::max takes references, and C++14 class constants have no definition.
            float const base = PB::HORIZON_SEC;
            float const maxH = PB::MAX_HORIZON_SEC;
            float h = z.kind == HazardKind::Pulse ? base : std::max(base, z.intervalSec);
            h = std::min(h, maxH);
            return std::max(1.0f, std::min(h, Value::FightRemainingSec(world)));
        }

        bool Escapes(HazardZone const& z, float walkSec)
        {
            return z.casting && walkSec + PB::DODGE_SLACK_SEC <= z.castRemainingSec;
        }

        // Hits of `z` within its horizon. walkSec < 0: standing in it. Otherwise: hits saved by
        // leaving with that walk (the cast in progress only if the bot is out before it ends).
        float Hits(WorldModel const& world, HazardZone const& z, float walkSec)
        {
            float const h = HorizonSec(world, z);
            if (!z.casting)
                return h / z.intervalSec;
            float const later = std::max(0.0f, h - z.castRemainingSec) / z.intervalSec;
            bool const current = walkSec < 0.0f || Escapes(z, walkSec);
            return later + (current ? 1.0f : 0.0f);
        }

        void AddLoss(WorldModel const& world, HazardZone const& z, PartyMemberFacts const& self, float hits, Loss& loss)
        {
            float const n = hits * z.hitChance;
            loss.points += n * HazardZones::HitPoints(world, z, self);
            loss.damage += n * z.damage;
        }

        // Healer: the member most enemies are hitting; a tank bot wins ties and counts even
        // when nothing hits it yet.
        bool FindHolder(WorldModel const& world, ObjectGuid self, Position3& out)
        {
            float best = 0.0f;
            for (PartyMemberFacts const& m : world.party)
            {
                if (m.isPet || !m.alive || m.guid == self)
                    continue;
                float score = Tanks::IsTank(m) ? 5.0f : 0.0f;
                for (EnemyFacts const& e : world.enemies)
                    if (e.acting && e.victim == m.guid)
                        score += 10.0f;
                if (score > best)
                {
                    best = score;
                    out = m.pos;
                }
            }
            return best > 0.0f;
        }

        bool Los(Map* map, Position3 const& a, Position3 const& b)
        {
            return map->isInLineOfSight(a.x, a.y, a.z + PB::LOS_EYE_HEIGHT, b.x, b.y, b.z + PB::LOS_EYE_HEIGHT);
        }

        // Distance-only checks (no map queries). `allowRange`: Range zones may contain the point.
        bool CheapOk(Context const& ctx, WorldModel const& /*world*/, std::vector<HazardZone> const& zones,
                     Position3 const& p, bool allowRange)
        {
            for (HazardZone const& z : zones)
            {
                if (!z.DangerousTo(ctx.roleBit) || (allowRange && z.kind == HazardKind::Range))
                    continue;
                if (Contains(z, p))
                    return false;
            }
            // Not where an idle creature would attack the bot (SafeRoute: scanned around the
            // bot itself). Fix: world.packs only covers PACK_SCAN_RADIUS around the leader; a
            // mage 110 yd away stepped into the next pack (strat-20261101-224637).
            if (ctx.idle && ctx.idle->Near(p))
                return false;
            if (ctx.roleBit == HAZARD_HEALER && ctx.hasHolder && PackResolver::Distance(p, ctx.holder) > PB::HEAL_RANGE)
                return false;
            if (ctx.roleBit == HAZARD_RANGED && ctx.victim && PackResolver::Distance(p, ctx.victimPos) > PB::RANGED_RANGE)
                return false;
            return true;
        }

        // The zone's enemy is chasing the bot in melee and is free to follow (not rooted).
        bool ChasesMe(Context const& ctx, WorldModel const& world, HazardZone const& z)
        {
            if (ctx.roleBit == HAZARD_MELEE)
                return false;   // melee stands next to it on purpose
            EnemyFacts const* e = world.FindEnemy(z.enemy);
            if (!e || e->victim != ctx.self->guid || !e->meleeEnabled || e->crowdControlled)
                return false;
            Unit const* unit = ctx.map->GetUnit(z.enemy);
            return unit && !unit->HasUnitState(UNIT_STATE_ROOT);
        }

        bool KeepsReach(Context const& ctx, Position3 const& p)
        {
            return ctx.roleBit == HAZARD_MELEE && ctx.victim &&
                   ctx.me->CanReachWithMeleeAutoAttackAtPosition(ctx.victim, p.x, p.y, p.z, -0.5f);
        }

        // Map queries: ground, distance checks again at the real height, line of sight.
        bool GeometryOk(Context const& ctx, WorldModel const& world, std::vector<HazardZone> const& zones,
                        Spot& s, bool allowRange)
        {
            float const ground = ctx.map->GetHeight(s.pos.x, s.pos.y, ctx.here.z + 3.0f);
            if (ground <= INVALID_HEIGHT || std::fabs(ground - ctx.here.z) > PB::MAX_STEP_Z)
                return false;
            s.pos.z = ground;
            if (!CheapOk(ctx, world, zones, s.pos, allowRange))
                return false;
            if (!Los(ctx.map, ctx.here, s.pos))
                return false;
            // The straight walk there (line of sight above) must not pass an idle creature either.
            if (ctx.idle && !SafeRoute::SegmentIsSafe(*ctx.idle, ctx.here, s.pos))
                return false;
            if (ctx.roleBit == HAZARD_HEALER && ctx.hasHolder && !Los(ctx.map, s.pos, ctx.holder))
                return false;
            if (ctx.roleBit == HAZARD_RANGED && ctx.victim && !Los(ctx.map, s.pos, ctx.victimPos))
                return false;
            s.keepsReach = KeepsReach(ctx, s.pos);
            return true;
        }

        bool FindSpot(Context const& ctx, WorldModel const& world, std::vector<HazardZone> const& zones,
                      HazardZone const& zone, bool allowRange, uint32& checks, Spot& out)
        {
            std::vector<Spot> spots;
            float radii[2] = { zone.radius + PB::RING_EXTRA, zone.radius + PB::MELEE_RING_EXTRA };
            uint32 const rings = ctx.roleBit == HAZARD_MELEE ? 2 : 1;
            float const start = std::atan2(ctx.here.y - zone.center.y, ctx.here.x - zone.center.x);
            for (uint32 r = 0; r < rings; ++r)
            {
                for (uint32 k = 0; k < PB::RING_POINTS; ++k)
                {
                    float const a = start + 2.0f * M_PI_F * float(k) / float(PB::RING_POINTS);
                    Spot s;
                    s.pos.x = zone.center.x + radii[r] * std::cos(a);
                    s.pos.y = zone.center.y + radii[r] * std::sin(a);
                    s.pos.z = ctx.here.z;
                    if (!CheapOk(ctx, world, zones, s.pos, allowRange))
                        continue;
                    s.dist = PackResolver::Distance(ctx.here, s.pos);
                    s.keepsReach = KeepsReach(ctx, s.pos);
                    spots.push_back(s);
                }
            }
            // Melee: points that keep the victim in reach first. Then nearest.
            std::sort(spots.begin(), spots.end(), [](Spot const& a, Spot const& b)
            {
                if (a.keepsReach != b.keepsReach)
                    return a.keepsReach;
                return a.dist < b.dist;
            });
            for (Spot& s : spots)
            {
                if (checks >= PB::MAX_GEOMETRY_CHECKS)
                    return false;
                ++checks;
                if (GeometryOk(ctx, world, zones, s, allowRange))
                {
                    out = s;
                    return true;
                }
            }
            return false;
        }

        // Melee: updates the record of the last out-of-reach step (header "Melee out of reach")
        // and returns true while the bot is back inside that area after the step and its hold.
        bool MeleeGaveUp(PartyBotAI::HazardGiveUp& g, std::vector<HazardZone> const& zones,
                         Position3 const& here, uint32 nowMs)
        {
            if (g.enemy.IsEmpty())
                return false;
            HazardZone const* zone = nullptr;
            for (HazardZone const& z : zones)
            {
                if (z.enemy == g.enemy && z.spellId == g.spellId)
                {
                    zone = &z;
                    break;
                }
            }
            uint32 const sinceMs = WorldTimer::getMSTimeDiff(g.movedMs, nowMs);
            bool const late = sinceMs >= g.holdMs + PB::MELEE_RETURN_MS;
            bool clear = !zone;
            if (zone && !Contains(*zone, here))
            {
                // Out: the step happened. Re-armed once the bot leaves again after coming back,
                // or when it does not come back (the step was not undone by the chase).
                g.left = true;
                clear = g.returned || late;
            }
            else if (zone)
            {
                if (g.left && sinceMs >= g.holdMs)
                    g.returned = true;
                else if (!g.left && late)
                    clear = true;       // the step was never executed (lost to another proposal)
            }
            if (clear)
            {
                g = PartyBotAI::HazardGiveUp();
                return false;
            }
            return g.returned;
        }

        char const* Reason(HazardZone const& primary, bool lethal, bool dodge)
        {
            if (lethal)
                return "leave lethal area";
            if (dodge)
                return "dodge area cast";
            if (primary.kind == HazardKind::Range)
                return "leave control range";
            return primary.control ? "leave control area" : "leave damage area";
        }

        // Value of moving to `spot` (header comment); false if nothing is gained.
        // `outOfReach`: melee step that leaves the victim's reach for the horizon (not a dodge).
        bool MakeProposal(Context const& ctx, WorldModel const& world, std::vector<HazardZone> const& zones,
                          HazardZone const& primary, Spot const& spot, Loss const& stay, Proposal& p,
                          bool& outOfReach)
        {
            outOfReach = false;
            PartyMemberFacts const& self = *ctx.self;
            float const walkSec = spot.dist * PB::PATH_FACTOR / ctx.speed;

            Loss saved, added;
            float awaySec = 0.0f;       // melee: time out of reach (no dodge)
            float dodgeUntil = 0.0f;    // latest end of a cast this move dodges
            bool allDodges = true;
            bool anyEscaped = false;
            for (HazardZone const& z : zones)
            {
                if (!z.DangerousTo(ctx.roleBit))
                    continue;
                bool const inHere = Contains(z, ctx.here);
                bool const inThere = Contains(z, spot.pos);
                if (inHere && !inThere)
                {
                    anyEscaped = true;
                    AddLoss(world, z, self, Hits(world, z, walkSec), saved);
                    if (Escapes(z, walkSec))
                        dodgeUntil = std::max(dodgeUntil, z.castRemainingSec);
                    else
                        allDodges = false;
                    awaySec = std::max(awaySec, HorizonSec(world, z));
                }
                else if (!inHere && inThere)
                    AddLoss(world, z, self, Hits(world, z, -1.0f), added);
            }
            if (!anyEscaped)
                return false;

            float benefit = saved.points - added.points;
            float const moveDamage = stay.damage - saved.damage + added.damage;
            bool const lethal = stay.damage >= float(self.health) && moveDamage < float(self.health);
            if (lethal)
                benefit += Value::DeathPenalty(self);

            float holdSec = walkSec + PB::HOLD_EXTRA_MS / 1000.0f;
            if (dodgeUntil > 0.0f)
                holdSec = std::max(holdSec, dodgeUntil + PB::DODGE_SLACK_SEC);
            float cost;
            if (ctx.roleBit == HAZARD_MELEE && !spot.keepsReach)
            {
                // Out of melee until the dodged cast ends, or for the horizon.
                float const away = allDodges ? dodgeUntil : awaySec;
                cost = Value::LostAction(world, self, walkSec + away);
                // Fix: away for the horizon held up to MAX_HORIZON_SEC and, re-proposed after
                // each chase back in, kept the bot out of melee for most of the fight. Hold at
                // most MELEE_AWAY_MAX_SEC; Propose then lets it fight inside (MeleeGaveUp).
                // Benefit and cost both stay counted over the horizon.
                float const maxAway = PB::MELEE_AWAY_MAX_SEC;
                holdSec = std::max(holdSec, walkSec + (allDodges ? away : std::min(away, maxAway)));
                outOfReach = !allDodges;
            }
            else
                cost = Value::LostAction(world, self, walkSec);

            p.tier = lethal ? ProposalTier::Urgent : ProposalTier::Assigned;
            p.source = "position";
            p.reason = Reason(primary, lethal, dodgeUntil > 0.0f);
            p.action.type = BotActionType::MoveTo;
            p.action.x = spot.pos.x;
            p.action.y = spot.pos.y;
            p.action.z = spot.pos.z;
            p.action.holdMs = uint32(holdSec * 1000.0f);
            p.SetValue(benefit, cost);
            return true;
        }
    }

    bool PositioningBehavior::AppliesTo(PartyBotAI const* bot) const
    {
        return HazardZones::RoleBit(bot->GetRole()) != 0;
    }

    namespace
    {
        // Movement belongs to another module (Propose's first checks).
        bool MovementOwnedElsewhere(TeamPlan const& plan)
        {
            return plan.retreat.active || plan.pull.phase == PullPhase::Approach || plan.pull.phase == PullPhase::Return;
        }
    }

    bool PositioningBehavior::WantsTickNow(PartyBotAI const* bot, WorldModel const& world, TeamPlan const& plan) const
    {
        if (MovementOwnedElsewhere(plan))
            return false;
        PartyMemberFacts const* self = world.FindMember(bot->me->GetObjectGuid());
        uint8 const roleBit = HazardZones::RoleBit(bot->GetRole());
        if (!self || !self->CanAct() || !roleBit)
            return false;
        for (HazardZone const& z : HazardZones::Get(world))
            if (z.DangerousTo(roleBit) && Contains(z, self->pos, ENTER_MARGIN))
                return true;
        return false;
    }

    float PositioningBehavior::CastBreakBenefit(PartyBotAI const* bot, WorldModel const& world, TeamPlan const& plan,
                                                uint32 castLeftMs, char const*& reason) const
    {
        if (MovementOwnedElsewhere(plan))
            return 0.0f;
        Player* me = bot->me;
        if (me->HasUnitState(UNIT_STATE_ROOT))
            return 0.0f;
        Context ctx;
        ctx.roleBit = HazardZones::RoleBit(bot->GetRole());
        ctx.self = world.FindMember(me->GetObjectGuid());
        if (!ctx.roleBit || !ctx.self || !ctx.self->CanAct())
            return 0.0f;
        ctx.me = me;
        ctx.map = me->GetMap();
        ctx.here.x = me->GetPositionX();
        ctx.here.y = me->GetPositionY();
        ctx.here.z = me->GetPositionZ();
        float const speed = std::max(1.0f, me->GetSpeed(MOVE_RUN));
        float const castLeftSec = castLeftMs / 1000.0f;

        Loss waiting;
        bool dodge = false;
        for (HazardZone const& z : HazardZones::Get(world))
        {
            if (!z.DangerousTo(ctx.roleBit) || !Contains(z, ctx.here, ENTER_MARGIN) || ChasesMe(ctx, world, z))
                continue;
            float const toEdge = std::max(0.0f, z.radius + RING_EXTRA - HazardZones::DistanceTo(z, ctx.here));
            float const walkSec = toEdge * PATH_FACTOR / speed;
            float hits;
            if (z.casting)
            {
                // The cast in progress: dodged now, not after the own cast.
                hits = Escapes(z, walkSec) && !Escapes(z, castLeftSec + walkSec) ? 1.0f : 0.0f;
                dodge = dodge || hits > 0.0f;
            }
            else
                hits = castLeftSec / z.intervalSec;
            AddLoss(world, z, *ctx.self, hits, waiting);
        }
        if (waiting.points <= 0.0f)
            return 0.0f;
        float benefit = waiting.points;
        bool const lethal = waiting.damage >= float(ctx.self->health);
        if (lethal)
            benefit += Value::DeathPenalty(*ctx.self);
        reason = lethal ? "area: staying until the cast ends kills" :
                 dodge  ? "area: dodge the area cast before mine ends" : "area: hits while finishing the cast";
        return benefit;
    }

    void PositioningBehavior::Propose(PartyBotAI const* bot, WorldModel const& world,
                                      TeamPlan const& plan, std::vector<Proposal>& out) const
    {
        // The retreat owns movement (Override tier); the pull owns it while the tank goes out
        // and brings the pack back.
        if (plan.retreat.active)
            return;
        if (plan.pull.phase == PullPhase::Approach || plan.pull.phase == PullPhase::Return)
            return;

        Player* me = bot->me;
        if (!world.InFight(me->IsInCombat()) || me->HasUnitState(UNIT_STATE_ROOT))
            return;
        Context ctx;
        ctx.roleBit = HazardZones::RoleBit(bot->GetRole());

        // A MoveTo (this module's or another's) is under way: let it finish.
        if (bot->IsMovementHeld() && me->IsMoving())
        {
            // Fix: without a proposal the original rotation runs on the next tick and its first
            // cast-time spell (a heal, a Frostbolt) stops the walk (DoCastSpell -> StopMoving):
            // the bot stood in the zone casting and only got out one tick of walking per cast.
            // Healers and ranged hold still-walking moves; score 0 loses to every valued
            // proposal of the same tier (benefit > cost), and Urgent proposals still win.
            // Other movers of healers / ranged (pull, retreat) run while this module is off;
            // a UseObject walk is valued and wins over this Hold.
            if (ctx.roleBit == HAZARD_HEALER || ctx.roleBit == HAZARD_RANGED)
            {
                Proposal hold;
                hold.tier = ProposalTier::Assigned;
                hold.score = 0.0f;
                hold.source = "position";
                hold.reason = "walking out of an area, keep walking";
                hold.action.type = BotActionType::Hold;
                out.push_back(hold);
            }
            return;
        }
        ctx.self = world.FindMember(me->GetObjectGuid());
        if (!ctx.roleBit || !ctx.self || !ctx.self->CanAct())
            return;

        std::vector<HazardZone> const& zones = HazardZones::Get(world);

        ctx.me = me;
        ctx.map = me->GetMap();
        ctx.here.x = me->GetPositionX();
        ctx.here.y = me->GetPositionY();
        ctx.here.z = me->GetPositionZ();

        uint32 const nowMs = WorldTimer::getMSTime();
        bool const gaveUp = ctx.roleBit == HAZARD_MELEE && MeleeGaveUp(bot->m_hazardGiveUp, zones, ctx.here, nowMs);
        if (zones.empty())
            return;

        // Zones the bot is clearly inside (trigger) and the expected loss of staying.
        std::vector<HazardZone const*> inside;
        Loss stay;
        for (HazardZone const& z : zones)
        {
            if (!z.DangerousTo(ctx.roleBit) || !Contains(z, ctx.here))
                continue;
            // The area of an enemy chasing this bot moves with it: a step out is undone by the
            // chase, and stepping straight away again and again walked a mage 100 yd from the
            // party (strat-20261101-224637). Running to the tank is EscapeBehavior's job.
            if (ChasesMe(ctx, world, z))
                continue;
            AddLoss(world, z, *ctx.self, Hits(world, z, -1.0f), stay);
            if (Contains(z, ctx.here, ENTER_MARGIN))
                inside.push_back(&z);
        }
        if (inside.empty() || stay.points <= 0.0f)
            return;
        // Worst zone first: its ring is tried first.
        std::sort(inside.begin(), inside.end(), [&](HazardZone const* a, HazardZone const* b)
        {
            return a->hitChance * HazardZones::HitPoints(world, *a, *ctx.self) / a->intervalSec >
                   b->hitChance * HazardZones::HitPoints(world, *b, *ctx.self) / b->intervalSec;
        });

        // Scanned only once the bot is in a zone (a grid visit; a few ticks per area).
        SafeRoute::IdleCreatures const idle(me);
        ctx.idle = &idle;

        ctx.speed = std::max(1.0f, me->GetSpeed(MOVE_RUN));
        if (ctx.roleBit == HAZARD_HEALER)
            ctx.hasHolder = FindHolder(world, me->GetObjectGuid(), ctx.holder);
        else if (Unit* victim = me->GetVictim())
        {
            if (victim->IsAlive())
            {
                ctx.victim = victim;
                ctx.victimPos.x = victim->GetPositionX();
                ctx.victimPos.y = victim->GetPositionY();
                ctx.victimPos.z = victim->GetPositionZ();
            }
        }

        bool const hasRangeZone = std::any_of(zones.begin(), zones.end(),
            [&](HazardZone const& z) { return z.kind == HazardKind::Range && z.DangerousTo(ctx.roleBit); });

        // Pass 1: out of every zone. Pass 2: Range zones (second priority) may stay.
        uint32 checks = 0;
        for (uint32 pass = 0; pass < (hasRangeZone ? 2u : 1u); ++pass)
        {
            bool const allowRange = pass == 1;
            uint32 tried = 0;
            for (HazardZone const* zone : inside)
            {
                if (tried++ >= MAX_ZONES_TRIED || checks >= MAX_GEOMETRY_CHECKS)
                    break;
                if (allowRange && zone->kind == HazardKind::Range)
                    continue;
                Spot spot;
                if (!FindSpot(ctx, world, zones, *zone, allowRange, checks, spot))
                    continue;
                Proposal p;
                bool outOfReach = false;
                if (MakeProposal(ctx, world, zones, *zone, spot, stay, p, outOfReach))
                {
                    if (outOfReach)
                    {
                        // Already stepped out once and came back: fight inside. Lethal steps still go.
                        // Only for that same area: a step out of another one still goes.
                        PartyBotAI::HazardGiveUp const& last = bot->m_hazardGiveUp;
                        if (gaveUp && p.tier != ProposalTier::Urgent &&
                            last.enemy == zone->enemy && last.spellId == zone->spellId)
                            continue;
                        PartyBotAI::HazardGiveUp& g = bot->m_hazardGiveUp;
                        g = PartyBotAI::HazardGiveUp();
                        g.enemy = zone->enemy;
                        g.spellId = zone->spellId;
                        g.movedMs = nowMs;
                        g.holdMs = p.action.holdMs;
                    }
                    out.push_back(p);
                    return;
                }
            }
        }
    }
}
