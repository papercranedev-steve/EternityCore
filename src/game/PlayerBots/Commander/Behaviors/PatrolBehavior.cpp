#include "Commander/Behaviors/PatrolBehavior.h"
#include "Commander/Behaviors/SafeRoute.h"
#include "Commander/Coordination/PatrolAssigner.h"
#include "Commander/Coordination/TeamPlan.h"
#include "PartyBotAI.h"
#include "Map.h"
#include <algorithm>
#include <cmath>
#include <functional>

namespace Commander
{
    namespace
    {
        Position3 Here(Player const* me)
        {
            Position3 here;
            here.x = me->GetPositionX();
            here.y = me->GetPositionY();
            here.z = me->GetPositionZ();
            return here;
        }

        // `s` with its ground height, within MAX_STEP_Z of `nearZ`, outside every idle
        // creature's reach and reachable on a safe path.
        bool SpotOk(Player* me, SafeRoute::IdleCreatures const& idle, float nearZ, Position3& s)
        {
            float const ground = me->GetMap()->GetHeight(s.x, s.y, nearZ + 3.0f);
            if (ground <= INVALID_HEIGHT || std::fabs(ground - nearZ) > PatrolBehavior::MAX_STEP_Z)
                return false;
            s.z = ground;
            return !idle.Near(s) && SafeRoute::PathIsSafe(me, idle, s, PatrolBehavior::MAX_PATH_DETOUR);
        }

        uint32 WalkHoldMs(Player const* me, float distance)
        {
            float const speed = std::max(1.0f, me->GetSpeed(MOVE_RUN));
            return uint32(distance * PatrolBehavior::PATH_FACTOR / speed * 1000.0f) + PatrolBehavior::HOLD_EXTRA_MS;
        }

        void ProposeTank(PartyBotAI const* bot, PatrolMovePlan const& m, std::vector<Proposal>& out)
        {
            Player* me = bot->me;
            Proposal p;
            p.tier = ProposalTier::Assigned;
            p.source = "patrol";

            // Walking there forwards (no backwards path): keep walking (an instant ability is fine,
            // a cast would stop the walk). Walking backwards: the order below is renewed instead,
            // and the rotation keeps fighting.
            if (bot->IsMovementHeld() && me->IsMoving() && !bot->IsBackpedaling())
            {
                p.reason = "moving the fight off a patrol's path, keep walking";
                p.action.type = BotActionType::Hold;
                out.push_back(p);
                return;
            }

            Position3 const here = Here(me);
            if (m.chosen == -1)
            {
                SafeRoute::IdleCreatures const idle(me);
                m.chosen = -2;
                for (size_t i = 0; i < m.spots.size(); ++i)
                {
                    Position3 s = m.spots[i];
                    if (!SpotOk(me, idle, here.z, s))
                        continue;
                    m.chosen = int32(i);
                    m.chosenSpot = s;
                    break;
                }
            }
            if (m.chosen < 0)
                return;

            Position3 const& to = m.chosenSpot;
            float const distance = SafeRoute::Distance2d(here, to);
            if (distance <= PatrolAssigner::ARRIVED_DISTANCE)
                return;
            p.reason = "move the fight off a patrol's path";
            p.action.x = to.x;
            p.action.y = to.y;
            p.action.z = to.z;
            p.SetValue(m.benefit, m.cost);

            // Backwards, facing what it fights (Backpedal.h): the pack follows without hitting the
            // tank's back. The forward walk is the fallback when no backwards path is found.
            if (Unit* victim = me->GetVictim())
            {
                Proposal back = p;
                back.action.type = BotActionType::Backpedal;
                back.action.target = victim->GetObjectGuid();
                back.action.holdMs = PatrolBehavior::BACKPEDAL_ORDER_MS;
                back.score += 1.0f;
                out.push_back(back);
            }
            p.action.type = BotActionType::MoveTo;
            p.action.holdMs = WalkHoldMs(me, distance);
            out.push_back(p);
        }

        void ProposeFollower(PartyBotAI const* bot, PatrolMovePlan const& m, std::vector<Proposal>& out)
        {
            // Waits for the tank's spot: the shift is only known then.
            if (m.chosen < 0)
                return;
            Player* me = bot->me;
            auto f = std::find_if(m.followers.begin(), m.followers.end(),
                [me](PatrolFollower const& x) { return x.guid == me->GetObjectGuid(); });
            if (f == m.followers.end() || f->state < 0)
                return;

            Proposal p;
            p.tier = ProposalTier::Assigned;
            p.source = "patrol";

            // A MoveTo is under way: without a proposal the rotation's first cast-time spell
            // stops the walk (DoCastSpell -> StopMoving).
            if (bot->IsMovementHeld() && me->IsMoving())
            {
                p.reason = "moving with the tank off a patrol's path, keep walking";
                p.action.type = BotActionType::Hold;
                out.push_back(p);
                return;
            }

            Position3 const here = Here(me);
            if (f->state == 0)
            {
                Position3 s = f->start;
                s.x += m.chosenSpot.x - m.tankStart.x;
                s.y += m.chosenSpot.y - m.tankStart.y;
                SafeRoute::IdleCreatures const idle(me);
                if (SpotOk(me, idle, here.z, s))
                {
                    f->state = 1;
                    f->target = s;
                }
                else
                    f->state = -1;
            }
            if (f->state < 0)
                return;

            float const distance = SafeRoute::Distance2d(here, f->target);
            if (distance <= PatrolAssigner::ARRIVED_DISTANCE)
                return;
            // The cast in progress (a heal) finishes first.
            if (me->IsNonMeleeSpellCasted(false, false, true))
                return;

            p.reason = "move with the tank off a patrol's path";
            p.action.type = BotActionType::MoveTo;
            p.action.x = f->target.x;
            p.action.y = f->target.y;
            p.action.z = f->target.z;
            p.action.holdMs = WalkHoldMs(me, distance);
            p.SetValue(m.benefit, m.cost);
            out.push_back(p);
        }

        bool IsMeleeRole(PartyBotAI const* bot)
        {
            return bot->GetRole() == ROLE_TANK || bot->GetRole() == ROLE_MELEE_DPS;
        }

        // Nearest enemy in sight that is not off limits (and not a caster of `lure`), within
        // reach of `from`: melee reach while hiding (movement is held), else TARGET_RANGE.
        Unit* PickTarget(PartyBotAI const* bot, WorldModel const& world, Position3 const& from,
                         PatrolLure const* lure, std::function<bool(ObjectGuid)> const& offLimits)
        {
            Player* me = bot->me;
            float const reach = lure && IsMeleeRole(bot) ? PatrolBehavior::HIDE_MELEE_REACH : PatrolBehavior::TARGET_RANGE;
            Unit* best = nullptr;
            float bestDistance = 0.0f;
            for (EnemyFacts const& e : world.enemies)
            {
                if (!e.acting || e.crowdControlled || !e.health || e.onAllyOnly || offLimits(e.guid))
                    continue;
                if (lure && std::find(lure->casters.begin(), lure->casters.end(), e.guid) != lure->casters.end())
                    continue;
                float const distance = SafeRoute::Distance2d(from, e.pos) - e.boundingRadius;
                if (distance > reach || (best && distance >= bestDistance))
                    continue;
                Unit* unit = me->GetMap()->GetUnit(e.guid);
                if (!unit || !unit->IsAlive() || !bot->IsValidHostileTarget(unit) || !me->IsWithinLOSInMap(unit))
                    continue;
                best = unit;
                bestDistance = distance;
            }
            return best;
        }

        Proposal MakeGuard(BotActionType type, float score, char const* reason)
        {
            Proposal p;
            p.tier = ProposalTier::Assigned;
            p.source = "patrol";
            p.score = score;
            p.reason = reason;
            p.action.type = type;
            return p;
        }

        // Guard (PatrolAssigner.h): hide from casters in the patrol's path, and stay off the
        // enemies standing in it.
        void ProposeGuard(PartyBotAI const* bot, WorldModel const& world, TeamPlan const& plan,
                          std::vector<Proposal>& out)
        {
            PatrolMovePlan const& m = plan.patrolMove;
            Player* me = bot->me;
            ObjectGuid const self = me->GetObjectGuid();
            InterceptDuty const* duty = plan.FindInterceptFor(self);
            auto const offLimits = [&m, duty](ObjectGuid g) { return m.Avoids(g) && !(duty && duty->enemy == g); };
            bool const healer = bot->GetRole() == ROLE_HEALER;
            Unit* victim = me->GetVictim();
            ObjectGuid const victimGuid = victim ? victim->GetObjectGuid() : ObjectGuid();

            if (PatrolLure const* lure = m.ActiveLureOf(self))
            {
                // Keep fighting while hiding: something in reach of the point, not the casters.
                bool const victimHidden = victim &&
                    (offLimits(victimGuid) ||
                     std::find(lure->casters.begin(), lure->casters.end(), victimGuid) != lure->casters.end());
                ObjectGuid face = victimHidden ? ObjectGuid() : victimGuid;
                // Only under the Backpedal hold: an Attack without it starts a chase
                // (AttackStart -> BeginChasing) away from the point. The first tick walks.
                if (!healer && (!victim || victimHidden) && bot->IsMovementHeld())
                {
                    if (Unit* other = PickTarget(bot, world, lure->point, lure, offLimits))
                    {
                        Proposal attack = MakeGuard(BotActionType::Attack, 6.0f, "fight on while hiding from a caster in the patrol's path");
                        attack.action.target = other->GetObjectGuid();
                        out.push_back(attack);
                        face = other->GetObjectGuid();
                    }
                }
                // Walk (backwards) to the point and stay there: the order is renewed every tick,
                // standing on the point too, so the chase does not take us back into its sight.
                Proposal back = MakeGuard(BotActionType::Backpedal, 5.0f, "out of sight of a caster in the patrol's path");
                back.action.target = face;
                back.action.x = lure->point.x;
                back.action.y = lure->point.y;
                back.action.z = lure->point.z;
                back.action.holdMs = PatrolBehavior::BACKPEDAL_ORDER_MS;
                out.push_back(back);
                if (!bot->IsMovementHeld())
                {
                    float const distance = SafeRoute::Distance2d(Here(me), lure->point);
                    Proposal forward = MakeGuard(BotActionType::MoveTo, 4.0f, "out of sight of a caster in the patrol's path");
                    forward.action.x = lure->point.x;
                    forward.action.y = lure->point.y;
                    forward.action.z = lure->point.z;
                    forward.action.holdMs = WalkHoldMs(me, distance);
                    out.push_back(forward);
                }
                return;
            }

            // A target standing in the patrol's path: TargetAssigner does not give it to anyone
            // (PatrolMovePlan::avoid is one of its limits), so the bot gets another one.
        }
    }

    bool PatrolBehavior::AppliesTo(PartyBotAI const* bot) const
    {
        uint8 const role = bot->GetRole();
        return role == ROLE_TANK || role == ROLE_HEALER || role == ROLE_RANGE_DPS || role == ROLE_MELEE_DPS;
    }

    void PatrolBehavior::Propose(PartyBotAI const* bot, WorldModel const& world,
                                 TeamPlan const& plan, std::vector<Proposal>& out) const
    {
        PatrolMovePlan const& m = plan.patrolMove;
        Player* me = bot->me;
        if (!m.active || me->HasUnitState(UNIT_STATE_ROOT))
            return;
        if (m.phase == PatrolPhase::Guard)
        {
            ProposeGuard(bot, world, plan, out);
            return;
        }
        if (m.spots.empty())
            return;
        if (m.tank == me->GetObjectGuid())
            ProposeTank(bot, m, out);
        else
            ProposeFollower(bot, m, out);
    }
}
