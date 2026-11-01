#include "Commander/Behaviors/FleePoint.h"
#include "Commander/Behaviors/SafeRoute.h"
#include "Commander/Perception/Tanks.h"
#include "PartyBotAI.h"
#include "Group.h"
#include "Map.h"
#include "Player.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace Commander
{
    namespace
    {
        using SafeRoute::Distance2d;

        Position3 PointOf(WorldObject const* obj)
        {
            Position3 p;
            p.x = obj->GetPositionX();
            p.y = obj->GetPositionY();
            p.z = obj->GetPositionZ();
            return p;
        }

        struct Candidate
        {
            Position3 pos;
            float score = 0.0f;
        };

        // Perception/Tanks.h when the party's world model is there; else a tank-role bot.
        bool IsTank(WorldModel const* world, Player* member)
        {
            if (world)
                if (PartyMemberFacts const* m = world->FindMember(member->GetObjectGuid()))
                    return Tanks::IsTank(*m);
            PartyBotAI* ai = member->AI() ? dynamic_cast<PartyBotAI*>(member->AI()) : nullptr;
            return ai && ai->GetRole() == ROLE_TANK;
        }
    }

namespace FleePoint
{
    bool Find(Player* me, Unit* enemy, WorldModel const* world, float& outX, float& outY, float& outZ)
    {
        Map* map = me->GetMap();
        Position3 const here = PointOf(me);
        Position3 const foe = PointOf(enemy);
        float const nowGap = Distance2d(here, foe);

        SafeRoute::IdleCreatures const idle(me);

        // Party members to run to, and the centre of the fighting party.
        std::vector<std::pair<Position3, bool>> allies;     // position, is a tank
        Position3 centre;
        uint32 counted = 0;
        if (Group* group = me->GetGroup())
        {
            for (GroupReference* itr = group->GetFirstMember(); itr; itr = itr->next())
            {
                Player* m = itr->getSource();
                if (!m || m == me || !m->IsInWorld() || m->GetMap() != map || !m->IsAlive())
                    continue;
                Position3 const p = PointOf(m);
                if (Distance2d(here, p) > MAX_ALLY_DISTANCE)
                    continue;
                centre.x += p.x;
                centre.y += p.y;
                ++counted;
                allies.emplace_back(p, IsTank(world, m));
            }
        }
        if (counted)
        {
            centre.x /= counted;
            centre.y /= counted;
        }
        else
            centre = here;

        std::vector<Candidate> candidates;
        auto consider = [&](Position3 p, bool toTank)
        {
            float const ground = map->GetHeight(p.x, p.y, here.z + MAX_HEIGHT_DIFFERENCE, true, 2.0f * MAX_HEIGHT_DIFFERENCE);
            if (ground <= INVALID_HEIGHT || std::fabs(ground - here.z) > MAX_HEIGHT_DIFFERENCE)
                return;
            p.z = ground;
            float const gap = Distance2d(p, foe);
            if (!toTank && (gap < MIN_ENEMY_GAP || gap < nowGap + MIN_GAIN))
                return;
            if (idle.Near(p))
                return;
            Candidate c;
            c.pos = p;
            c.score = std::min(gap, ENEMY_DISTANCE_CAP) - CENTRE_WEIGHT * Distance2d(p, centre) + (toTank ? TANK_BONUS : 0.0f);
            candidates.push_back(c);
        };

        for (auto const& ally : allies)
            consider(ally.first, ally.second);
        for (float distance : RING_DISTANCES)
        {
            for (uint32 i = 0; i < RING_ANGLES; ++i)
            {
                float const angle = 2.0f * float(M_PI) * i / RING_ANGLES;
                Position3 p;
                p.x = here.x + std::cos(angle) * distance;
                p.y = here.y + std::sin(angle) * distance;
                consider(p, false);
            }
        }

        std::sort(candidates.begin(), candidates.end(),
                  [](Candidate const& a, Candidate const& b) { return a.score > b.score; });

        uint32 checks = 0;
        for (Candidate const& c : candidates)
        {
            if (checks++ >= MAX_PATH_CHECKS)
                break;
            // The way there must not pass an idle pack either.
            if (!SafeRoute::PathIsSafe(me, idle, c.pos, MAX_PATH_DETOUR))
                continue;
            outX = c.pos.x;
            outY = c.pos.y;
            outZ = c.pos.z;
            return true;
        }
        return false;
    }
}
}
