#include "Commander/Behaviors/RetreatBehavior.h"
#include "Commander/Coordination/RetreatPlanner.h"
#include "Commander/Coordination/TeamPlan.h"
#include "PartyBotAI.h"
#include "Map.h"
#include <algorithm>
#include <cmath>

namespace Commander
{
    namespace
    {
        float Dist2(Position3 const& a, Position3 const& b)
        {
            float const dx = a.x - b.x, dy = a.y - b.y;
            return std::sqrt(dx * dx + dy * dy);
        }

        float Dist3(Position3 const& a, Position3 const& b)
        {
            float const dz = a.z - b.z;
            float const d2 = Dist2(a, b);
            return std::sqrt(d2 * d2 + dz * dz);
        }
    }

    bool RetreatBehavior::AppliesTo(PartyBotAI const* bot) const
    {
        return bot->GetRole() != ROLE_INVALID;
    }

    void RetreatBehavior::Propose(PartyBotAI const* bot, WorldModel const& world,
                                  TeamPlan const& plan, std::vector<Proposal>& out) const
    {
        RetreatPlan const& r = plan.retreat;
        if (!r.active)
            return;

        Player* me = bot->me;
        Position3 here;
        here.x = me->GetPositionX();
        here.y = me->GetPositionY();
        here.z = me->GetPositionZ();

        Proposal p;
        p.tier = ProposalTier::Override;
        p.score = 1.0f;
        p.source = "retreat";
        p.reason = "retreat ordered";
        p.action.type = BotActionType::Retreat;

        Position3 dest = r.point;
        bool move = Dist3(here, r.point) > RetreatAssigner::ARRIVED_DISTANCE;
        if (!move)
        {
            // Standing still next to a chaser for 5 s extends its leash (scenario 11, 1.5,
            // "caught up"): step to the far side of the point. The chaser follows, the bot
            // crosses again, so it never stands still for long.
            for (EnemyFacts const& e : world.enemies)
            {
                if (e.victim != me->GetObjectGuid() || Dist2(e.pos, here) > CHASER_DISTANCE)
                    continue;
                float dx = r.point.x - e.pos.x;
                float dy = r.point.y - e.pos.y;
                float const len = std::sqrt(dx * dx + dy * dy);
                if (len < 0.5f)
                {
                    // Enemy on the point: away from it, along the bot's own offset.
                    dx = here.x - e.pos.x;
                    dy = here.y - e.pos.y;
                }
                float const n = std::max(0.5f, std::sqrt(dx * dx + dy * dy));
                dest.x = r.point.x + dx / n * SIDESTEP_DISTANCE;
                dest.y = r.point.y + dy / n * SIDESTEP_DISTANCE;
                float const ground = me->GetMap()->GetHeight(dest.x, dest.y, r.point.z + 5.0f);
                dest.z = ground > INVALID_HEIGHT ? ground : r.point.z;
                move = true;
                break;
            }
        }

        if (move)
        {
            float const speed = std::max(1.0f, me->GetSpeed(MOVE_RUN));
            float const distance = Dist3(here, dest);
            p.action.x = dest.x;
            p.action.y = dest.y;
            p.action.z = dest.z;
            p.action.holdMs = uint32(distance * PATH_FACTOR / speed * 1000.0f) + HOLD_EXTRA_MS;
        }
        // holdMs = 0: stand at the point, still not attacking.
        out.push_back(p);
    }
}
