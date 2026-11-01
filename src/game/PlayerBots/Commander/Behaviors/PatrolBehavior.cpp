#include "Commander/Behaviors/PatrolBehavior.h"
#include "Commander/Behaviors/SafeRoute.h"
#include "Commander/Coordination/PatrolAssigner.h"
#include "Commander/Coordination/TeamPlan.h"
#include "PartyBotAI.h"
#include "Map.h"
#include <cmath>

namespace Commander
{
    bool PatrolBehavior::AppliesTo(PartyBotAI const* bot) const
    {
        return bot->GetRole() == ROLE_TANK;
    }

    void PatrolBehavior::Propose(PartyBotAI const* bot, WorldModel const& world,
                                 TeamPlan const& plan, std::vector<Proposal>& out) const
    {
        PatrolMovePlan const& m = plan.patrolMove;
        Player* me = bot->me;
        if (!m.active || m.tank != me->GetObjectGuid() || m.spots.empty() || me->HasUnitState(UNIT_STATE_ROOT))
            return;

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

        Position3 here;
        here.x = me->GetPositionX();
        here.y = me->GetPositionY();
        here.z = me->GetPositionZ();

        if (m.chosen == -1)
        {
            Map* map = me->GetMap();
            SafeRoute::IdleCreatures const idle(me);
            m.chosen = -2;
            for (size_t i = 0; i < m.spots.size(); ++i)
            {
                Position3 s = m.spots[i];
                float const ground = map->GetHeight(s.x, s.y, here.z + 3.0f);
                if (ground <= INVALID_HEIGHT || std::fabs(ground - here.z) > MAX_STEP_Z)
                    continue;
                s.z = ground;
                if (idle.Near(s) || !SafeRoute::PathIsSafe(me, idle, s, MAX_PATH_DETOUR))
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
            back.action.holdMs = BACKPEDAL_ORDER_MS;
            back.score += 1.0f;
            out.push_back(back);
        }
        float const speed = std::max(1.0f, me->GetSpeed(MOVE_RUN));
        p.action.type = BotActionType::MoveTo;
        p.action.holdMs = uint32(distance * PATH_FACTOR / speed * 1000.0f) + HOLD_EXTRA_MS;
        out.push_back(p);
    }
}
