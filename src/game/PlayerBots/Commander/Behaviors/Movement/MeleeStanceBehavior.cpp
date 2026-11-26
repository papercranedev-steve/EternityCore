#include "Commander/Behaviors/Movement/MeleeStanceBehavior.h"
#include "Commander/BotDirectives.h"
#include "Commander/Classes/ClassKit.h"
#include "Commander/Perception/Spatial/RouteRisk.h"
#include "Commander/Perception/Spatial/SafeRoute.h"
#include "Commander/Perception/Spatial/PositionField.h"
#include "Commander/Perception/Spatial/FieldSpots.h"
#include "Commander/Perception/Tanks.h"
#include "Commander/Perception/Values/ValueModel.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Coordination/Situation/Patrol/RouteGuard.h"         // StaySec: pure
#include "Commander/Perception/Spatial/PatrolZone.h"
#include "Commander/Perception/Spatial/HazardZones.h"
#include "Commander/Perception/Values/SpotCost.h"
#include "TeamBotAI.h"
#include "Map.h"
#include "Timer.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace Commander
{
    namespace
    {
        using MS = MeleeStanceBehavior;
        using SafeRoute::Distance2d;

        // From the victim's facing: behind first (the original chase), then to the sides.
        float const ANGLES[] = { float(M_PI), float(M_PI) * 0.75f, float(M_PI) * 1.25f,
                                 float(M_PI) * 0.5f, float(M_PI) * 1.5f };
        // Idle creatures around the victim this far are checked for the melee spots.
        float const SPOT_SCAN_YD = 40.0f;

        Position3 PositionOf(WorldObject const* obj)
        {
            Position3 p;
            p.x = obj->GetPositionX();
            p.y = obj->GetPositionY();
            p.z = obj->GetPositionZ();
            return p;
        }

        bool InPatrolPath(WorldModel const& world, Position3 const& p)
        {
            return PatrolZone::Comes(world, p, Value::FightRemainingSec(world));
        }

        EnemyFacts const* FactsOf(WorldModel const& world, ObjectGuid guid)
        {
            if (EnemyFacts const* e = world.FindEnemy(guid))
                return e;
            return world.FindTargeted(guid);
        }

        Proposal Make(ProposalTier tier, BotActionType type, char const* reason)
        {
            Proposal p;
            p.tier = tier;
            p.source = "meleestance";
            p.reason = reason;
            p.action.type = type;
            return p;
        }

        // Where the bot stands chasing `victim` at `angle` (MoveChase distance SPOT_GAP).
        Position3 SpotAt(Unit const* victim, float angle)
        {
            float const o = victim->GetOrientation() + angle;
            float const r = victim->GetCombatReach() + MS::SPOT_GAP;
            Position3 p = PositionOf(victim);
            p.x += r * std::cos(o);
            p.y += r * std::sin(o);
            return p;
        }

        bool SpotOk(WorldModel const& world, SafeRoute::IdleCreatures const& idle, Unit const* victim, float angle)
        {
            Position3 const p = SpotAt(victim, angle);
            return !idle.Near(p) && !InPatrolPath(world, p);
        }

        // Header "angle": standing at `s` in front of `victim` (its front half) lets it parry and
        // block: FRONT_LOSS_SHARE of the bot's output over the horizon.
        float SpotLoss(WorldModel const& world, PartyMemberFacts const& self, Unit const* victim,
                       Position3 const& s, float horizonSec)
        {
            float const facing = victim->GetOrientation();
            float const toSpot = std::atan2(s.y - victim->GetPositionY(), s.x - victim->GetPositionX());
            float diff = std::fabs(std::remainder(toSpot - facing, 2.0f * float(M_PI)));
            bool const inFront = diff < float(M_PI) * 0.5f;
            return inFront ? MS::FRONT_LOSS_SHARE * Value::LostAction(world, self, horizonSec) : 0.0f;
        }

        // Header "gap closer": the class's charge (Intercept) instead of the walk to `enemy`.
        // The caller checked the walk (RouteRisk::Blocked, MayEngage).
        void ProposeGapCloser(TeamBotAI const* bot, WorldModel const& world, PartyMemberFacts const& self,
                              Unit* enemy, std::vector<Proposal>& out)
        {
            SpellEntry const* spell = bot->Kit().GapCloser();
            if (!spell)
                return;
            // A moving enemy comes to the pile or is being brought there: the stun would keep it
            // where it is, away from the tank's spot.
            if (enemy->IsMoving())
                return;
            Player* me = bot->me;
            if (HazardZones::IsInside(HazardZones::Get(world), HAZARD_MELEE, PositionOf(enemy)))
                return;
            // (range, rage, stance swap, kept controls, the pull's "no charge": CanCastWithStance)
            if (!me->IsWithinLOSInMap(enemy) || !bot->CanCastWithStance(enemy, spell))
                return;
            float const speed = std::max(1.0f, me->GetSpeed(MOVE_RUN));
            float const walkSec = std::max(0.0f, me->GetDistance(enemy) - MS::MELEE_REACH) / speed;
            float const horizon = std::max(1.0f, std::min(MS::HORIZON_SEC, Value::FightRemainingSec(world)));
            Proposal p = Make(ProposalTier::Assigned, BotActionType::Cast, "charge to the victim instead of the walk");
            p.action.target = enemy->GetObjectGuid();
            p.action.spell = spell;
            p.SetValue(Value::LostAction(world, self, horizon) + Value::LostAction(world, self, walkSec),
                       Value::LostAction(world, self, Value::GCD_SEC));
            out.push_back(p);
        }

        bool IsListedAngle(float angle)
        {
            for (float a : ANGLES)
                if (std::fabs(a - angle) < 0.01f)
                    return true;
            return false;
        }

        // Chase `enemy`; false when its walk is blocked (the caller waits near the tank then).
        // `assigned`: the brain's target. Its walk is checked on the hard line: the brain sends a
        // bot over it only when no other enemy is open to it (DamageTargets, RouteFact::safeTight).
        bool ProposeChase(TeamBotAI const* bot, WorldModel const& world, TeamPlan const& plan,
                          PartyMemberFacts const& self, Unit* enemy, bool assigned, std::vector<Proposal>& out)
        {
            Player* me = bot->me;
            bool const inReach = me->CanReachWithMeleeAutoAttack(enemy);
            float current = 0.0f;
            bool const chasing = bot->GetChaseAngle(enemy, current);

            EnemyFacts const* facts = FactsOf(world, enemy->GetObjectGuid());
            bool const blocked = !inReach &&
                ((facts && RouteRisk::Blocked(world, self, *facts, assigned)) ||
                 !plan.MayEngage(self.guid, enemy->GetObjectGuid()));
            if (blocked)
            {
                if (chasing)
                    out.push_back(Make(ProposalTier::Assigned, BotActionType::StopChase,
                                       "the walk to the victim passes an idle pack"));
                return false;
            }

            // (next to the chase below: the arbiter takes the better one)
            if (!inReach)
                ProposeGapCloser(bot, world, self, enemy, out);

            float angle = float(M_PI);
            if (enemy->GetVictim() == me)
                angle = 0.0f;       // it faces the bot: an angle from its facing would circle it
            else
            {
                SafeRoute::IdleCreatures idle(me, PositionOf(enemy), SPOT_SCAN_YD);
                idle.Ignore({ enemy->GetObjectGuid() });
                float const horizon = std::max(1.0f, std::min(MS::HORIZON_SEC, Value::FightRemainingSec(world)));
                std::vector<HazardZone> const& zones = HazardZones::Get(world);
                bool const keep = chasing && IsListedAngle(current);
                Position3 const from = keep ? SpotAt(enemy, current) : PositionOf(me);
                float const speed = std::max(1.0f, me->GetSpeed(MOVE_RUN));
                float best = 0.0f;
                float currentLoss = -1.0f;
                bool found = false;
                for (float a : ANGLES)
                {
                    if (!SpotOk(world, idle, enemy, a))
                        continue;
                    Position3 const s = SpotAt(enemy, a);
                    // (an area is an exclusion here: stepping out of one is the position module's)
                    if (HazardZones::IsInside(zones, HAZARD_MELEE, s))
                        continue;
                    float const loss = SpotLoss(world, self, enemy, s, horizon) +
                                       SpotCost::Walk(world, self, Distance2d(from, s) / speed);
                    if (keep && std::fabs(a - current) < 0.01f)
                        currentLoss = loss;
                    if (!found || loss < best)
                    {
                        best = loss;
                        angle = a;
                        found = true;
                    }
                }
                // Its spot is still fine and not worse than the best by more than the margin.
                if (currentLoss >= 0.0f && currentLoss <= best + SpotCost::Walk(world, self, MS::MOVE_MARGIN_SEC))
                    return true;
            }
            if (chasing && std::fabs(current - angle) < 0.01f)
                return true;

            Proposal p = Make(ProposalTier::Assigned, BotActionType::Chase,
                              inReach ? "chase the victim at the chosen side" : "walk to the victim");
            p.action.target = enemy->GetObjectGuid();
            p.action.x = angle;
            float const horizon = std::max(1.0f, std::min(MS::HORIZON_SEC, Value::FightRemainingSec(world)));
            p.SetValue(Value::LostAction(world, self, inReach ? MS::KEEP_SEC : horizon), 0.0f);
            out.push_back(p);
            return true;
        }

        // No enemy it can walk to: wait near the tank (the leader without one).
        void ProposeWait(TeamBotAI const* bot, WorldModel const& world, TeamPlan const& plan,
                         PartyMemberFacts const& self, std::vector<Proposal>& out)
        {
            Player* me = bot->me;
            PartyMemberFacts const* anchor = Tanks::Main(world);
            if (!anchor || anchor->guid == self.guid)
                anchor = world.FindMember(world.leader);
            if (!anchor || anchor->guid == self.guid || !anchor->alive)
                return;
            Position3 const here = PositionOf(me);
            Position3 const& a = anchor->pos;
            float const distance = Distance2d(here, a);
            if (distance <= MS::WAIT_NEAR_YD)
                return;

            // Idle creatures along the whole walk.
            Position3 mid;
            mid.x = (here.x + a.x) * 0.5f;
            mid.y = (here.y + a.y) * 0.5f;
            mid.z = (here.z + a.z) * 0.5f;
            SafeRoute::IdleCreatures const idle(me, mid, distance * 0.5f + SafeRoute::SCAN_RADIUS * 0.5f);
            SafeRoute::IdleCreatures const pathIdle = idle.Near(here) ? idle.LeavingFrom(here) : idle;

            if (!world.field)
                return;     // built while the party fights (PositionFieldBuilder)
            // Field cells WAIT_MIN_YD..WAIT_MAX_YD from the anchor (ground linked to the party,
            // no idle creature, no patrol before the fight ends, a grid walk there), shortest walk first.
            FieldSpots::Query q;
            q.center = a;
            q.minYd = MS::WAIT_MIN_YD;
            q.maxYd = MS::WAIT_MAX_YD;
            q.patrolBeforeSec = RouteGuard::StaySec(world, plan);
            std::vector<FieldSpots::Spot> spots = FieldSpots::Collect(world, self, q);
            auto const walk = [](FieldSpots::Spot const& s) { return s.WalkEstimate(MS::PATH_FACTOR); };
            std::sort(spots.begin(), spots.end(), [&walk](FieldSpots::Spot const& x, FieldSpots::Spot const& y)
            {
                return walk(x) < walk(y);
            });
            FieldSpots::PreferFree(spots, walk, [](FieldSpots::Spot const& s) { return s.taken; });

            uint32 checks = 0;
            for (FieldSpots::Spot const& s : spots)
            {
                if (checks >= MS::MAX_GEOMETRY_CHECKS)
                    break;
                // This bot's own idle scan at the end (the field uses the party's farthest reach).
                if (idle.Near(s.pos))
                    continue;
                ++checks;
                if (!SafeRoute::PathIsSafe(me, pathIdle, s.pos, MS::MAX_PATH_DETOUR))
                    continue;

                float const speed = std::max(1.0f, me->GetSpeed(MOVE_RUN));
                float const walkSec = s.WalkEstimate(MS::PATH_FACTOR) / speed;
                Proposal p = Make(ProposalTier::Assigned, BotActionType::MoveTo,
                                  "no enemy it can walk to: wait near the tank");
                p.action.x = s.pos.x;
                p.action.y = s.pos.y;
                p.action.z = s.pos.z;
                p.action.holdMs = uint32(walkSec * 1000.0f) + MS::HOLD_EXTRA_MS;
                // The walk costs nothing now and is not walked once an enemy comes.
                p.SetValue(Value::LostAction(world, self, walkSec), 0.0f);
                out.push_back(p);
                return;
            }
        }
    }

    bool MeleeStanceBehavior::AppliesTo(TeamBotAI const* bot) const
    {
        // A healer in melee too (Doctrine/HealerDamage.h): only while it has an enemy (Propose).
        return bot->GetRole() == ROLE_MELEE_DPS || bot->IsMeleeHealer();
    }

    bool MeleeStanceBehavior::TakesOver(Takeover what, TeamBotAI const* bot, WorldModel const& world,
                                        TeamPlan const& /*plan*/) const
    {
        return what == Takeover::Stance && world.InFight(bot->me->IsInCombat()) &&
               !bot->me->GetTransport();
    }

    void MeleeStanceBehavior::Propose(TeamBotAI const* bot, WorldModel const& world,
                                      TeamPlan const& plan, std::vector<Proposal>& out) const
    {
        // The retreat owns movement (Override tier).
        if (plan.retreat.active)
            return;
        Player* me = bot->me;
        if (me->HasUnitState(UNIT_STATE_ROOT) || me->GetTransport())
            return;
        bool const inCombat = me->IsInCombat();
        if (!world.InFight(inCombat))
            return;
        // Still out of combat and told to wait by the pull (PullBehavior holds it).
        if (!inCombat && plan.pull.HoldsWaiting(false))
            return;
        PartyMemberFacts const* self = world.FindMember(me->GetObjectGuid());
        if (!self || !self->CanAct())
            return;

        // A walk under way: the position module's step out of an area (it brings the bot back
        // by ending its hold), or this module's walk to the tank (keep walking: the rotation's
        // first ability would not stop it, but a new chase would).
        uint32 const now = WorldTimer::getMSTime();
        if (bot->IsMovementHeld())
        {
            char const* owner = bot->Directives().GetMovementOwner(now);
            if (owner && !strcmp(owner, GetId()) && me->IsMoving())
            {
                Proposal hold = Make(ProposalTier::Assigned, BotActionType::Hold, "walking to the tank, keep walking");
                hold.score = 0.0f;
                out.push_back(hold);
            }
            return;
        }

        // The brain's target, else the victim.
        Unit* enemy = nullptr;
        if (TargetDuty const* duty = plan.FindTargetFor(me->GetObjectGuid()))
            enemy = me->GetMap()->GetUnit(duty->enemy);
        bool const assigned = enemy && enemy->IsAlive() && bot->IsValidHostileTarget(enemy);
        if (!assigned)
            enemy = me->GetVictim();
        if (enemy && enemy->IsAlive() && bot->IsValidHostileTarget(enemy) &&
            ProposeChase(bot, world, plan, *self, enemy, assigned, out))
            return;
        // A healer without an enemy to walk to: the healer stance places it.
        if (bot->GetRole() == ROLE_HEALER)
            return;
        ProposeWait(bot, world, plan, *self, out);
    }
}
