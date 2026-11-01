#include "Commander/Behaviors/PositioningBehavior.h"
#include "Commander/Perception/HazardZones.h"
#include "Commander/Perception/PackResolver.h"
#include "Commander/Perception/ValueModel.h"
#include "Commander/Coordination/TeamPlan.h"
#include "PartyBotAI.h"
#include "Map.h"
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
                float score = (m.isBot && m.role == ROLE_TANK) ? 5.0f : 0.0f;
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
        bool CheapOk(Context const& ctx, WorldModel const& world, std::vector<HazardZone> const& zones,
                     Position3 const& p, bool allowRange)
        {
            for (HazardZone const& z : zones)
            {
                if (!z.DangerousTo(ctx.roleBit) || (allowRange && z.kind == HazardKind::Range))
                    continue;
                if (Contains(z, p))
                    return false;
            }
            // Do not walk closer to an idle aggressive creature that is already within its aggro range.
            for (PackFacts const& pack : world.packs)
            {
                for (PackCreature const& c : pack.members)
                {
                    if (!c.aggressive || c.noAggro)
                        continue;
                    float const dNew = PackResolver::Distance(p, c.pos);
                    if (dNew < PB::PACK_CLEARANCE && dNew < PackResolver::Distance(ctx.here, c.pos))
                        return false;
                }
            }
            if (ctx.roleBit == HAZARD_HEALER && ctx.hasHolder && PackResolver::Distance(p, ctx.holder) > PB::HEAL_RANGE)
                return false;
            if (ctx.roleBit == HAZARD_RANGED && ctx.victim && PackResolver::Distance(p, ctx.victimPos) > PB::RANGED_RANGE)
                return false;
            return true;
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
        bool MakeProposal(Context const& ctx, WorldModel const& world, std::vector<HazardZone> const& zones,
                          HazardZone const& primary, Spot const& spot, Loss const& stay, Proposal& p)
        {
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
                holdSec = std::max(holdSec, walkSec + away);
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
        if (!me->IsInCombat() || me->HasUnitState(UNIT_STATE_ROOT))
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
        if (zones.empty())
            return;

        ctx.me = me;
        ctx.map = me->GetMap();
        ctx.here.x = me->GetPositionX();
        ctx.here.y = me->GetPositionY();
        ctx.here.z = me->GetPositionZ();

        // Zones the bot is clearly inside (trigger) and the expected loss of staying.
        std::vector<HazardZone const*> inside;
        Loss stay;
        for (HazardZone const& z : zones)
        {
            if (!z.DangerousTo(ctx.roleBit) || !Contains(z, ctx.here))
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
                if (MakeProposal(ctx, world, zones, *zone, spot, stay, p))
                {
                    out.push_back(p);
                    return;
                }
            }
        }
    }
}
