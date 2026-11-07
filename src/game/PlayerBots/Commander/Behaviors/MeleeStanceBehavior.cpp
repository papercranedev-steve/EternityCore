#include "Commander/Behaviors/MeleeStanceBehavior.h"
#include "Commander/BotDirectives.h"
#include "Commander/Perception/RouteRisk.h"
#include "Commander/Perception/SafeRoute.h"
#include "Commander/Perception/Tanks.h"
#include "Commander/Perception/ValueModel.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Coordination/PatrolAssigner.h"     // InPath: pure, facts only
#include "PartyBotAI.h"
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
        // Around the tank while waiting for an enemy it can walk to.
        float const WAIT_RADII[] = { 6.0f, 10.0f };
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
            float const remaining = Value::FightRemainingSec(world);
            for (PatrolFacts const& patrol : world.patrols)
                if (patrol.aggressive && !patrol.noAggro && !patrol.tappedByOther &&
                    PatrolAssigner::InPath(patrol, p, remaining))
                    return true;
            return false;
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

        bool IsListedAngle(float angle)
        {
            for (float a : ANGLES)
                if (std::fabs(a - angle) < 0.01f)
                    return true;
            return false;
        }

        // Chase `enemy`; false when its walk is blocked (the caller waits near the tank then).
        bool ProposeChase(PartyBotAI const* bot, WorldModel const& world, TeamPlan const& plan,
                          PartyMemberFacts const& self, Unit* enemy, std::vector<Proposal>& out)
        {
            Player* me = bot->me;
            bool const inReach = me->CanReachWithMeleeAutoAttack(enemy);
            float current = 0.0f;
            bool const chasing = bot->GetChaseAngle(enemy, current);

            EnemyFacts const* facts = FactsOf(world, enemy->GetObjectGuid());
            bool const blocked = !inReach &&
                ((facts && RouteRisk::Blocked(world, self, *facts)) || plan.patrolMove.Avoids(enemy->GetObjectGuid()));
            if (blocked)
            {
                if (chasing)
                    out.push_back(Make(ProposalTier::Assigned, BotActionType::StopChase,
                                       "the walk to the victim passes an idle pack"));
                return false;
            }

            float angle = float(M_PI);
            if (enemy->GetVictim() == me)
                angle = 0.0f;       // it faces the bot: an angle from its facing would circle it
            else
            {
                SafeRoute::IdleCreatures idle(me, PositionOf(enemy), SPOT_SCAN_YD);
                idle.Ignore({ enemy->GetObjectGuid() });
                if (chasing && IsListedAngle(current) && SpotOk(world, idle, enemy, current))
                    return true;    // its spot is still fine
                for (float a : ANGLES)
                {
                    if (SpotOk(world, idle, enemy, a))
                    {
                        angle = a;
                        break;
                    }
                }
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
        void ProposeWait(PartyBotAI const* bot, WorldModel const& world, PartyMemberFacts const& self,
                         std::vector<Proposal>& out)
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

            struct Spot
            {
                Position3 pos;
                float walk = 0.0f;
            };
            std::vector<Spot> spots;
            float const start = std::atan2(here.y - a.y, here.x - a.x);
            for (float r : WAIT_RADII)
            {
                for (uint32 k = 0; k < MS::RING_POINTS; ++k)
                {
                    // 0, +1, -1, +2, -2 ... steps away from the bot's side.
                    int32 const step = int32((k + 1) / 2) * ((k % 2) ? 1 : -1);
                    float const angle = start + 2.0f * float(M_PI) * step / MS::RING_POINTS;
                    Spot s;
                    s.pos.x = a.x + r * std::cos(angle);
                    s.pos.y = a.y + r * std::sin(angle);
                    s.pos.z = a.z;
                    if (idle.Near(s.pos) || InPatrolPath(world, s.pos))
                        continue;
                    s.walk = Distance2d(here, s.pos);
                    spots.push_back(s);
                }
            }
            std::stable_sort(spots.begin(), spots.end(), [](Spot const& x, Spot const& y) { return x.walk < y.walk; });

            Map* map = me->GetMap();
            uint32 checks = 0;
            for (Spot& s : spots)
            {
                if (checks >= MS::MAX_GEOMETRY_CHECKS)
                    break;
                ++checks;
                float const ground = map->GetHeight(s.pos.x, s.pos.y, a.z + 3.0f);
                if (ground <= INVALID_HEIGHT || std::fabs(ground - a.z) > MS::MAX_STEP_Z)
                    continue;
                s.pos.z = ground;
                if (!SafeRoute::PathIsSafe(me, pathIdle, s.pos, MS::MAX_PATH_DETOUR))
                    continue;

                float const speed = std::max(1.0f, me->GetSpeed(MOVE_RUN));
                float const walkSec = s.walk * MS::PATH_FACTOR / speed;
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

    bool MeleeStanceBehavior::AppliesTo(PartyBotAI const* bot) const
    {
        return bot->GetRole() == ROLE_MELEE_DPS;
    }

    bool MeleeStanceBehavior::TakesOver(Takeover what, PartyBotAI const* bot, WorldModel const& world) const
    {
        return what == Takeover::Stance && world.InFight(bot->me->IsInCombat()) && !bot->IsInDuel() &&
               !bot->me->GetTransport();
    }

    void MeleeStanceBehavior::Propose(PartyBotAI const* bot, WorldModel const& world,
                                      TeamPlan const& plan, std::vector<Proposal>& out) const
    {
        // The retreat owns movement (Override tier).
        if (plan.retreat.active)
            return;
        Player* me = bot->me;
        if (me->HasUnitState(UNIT_STATE_ROOT) || bot->IsInDuel() || me->GetTransport())
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
        if (!enemy || !enemy->IsAlive() || !bot->IsValidHostileTarget(enemy))
            enemy = me->GetVictim();
        if (enemy && enemy->IsAlive() && bot->IsValidHostileTarget(enemy) &&
            ProposeChase(bot, world, plan, *self, enemy, out))
            return;
        ProposeWait(bot, world, *self, out);
    }
}
