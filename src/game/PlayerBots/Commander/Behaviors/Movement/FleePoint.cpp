#include "Commander/Behaviors/Movement/FleePoint.h"
#include "Commander/Team.h"
#include "Commander/Perception/Spatial/SafeRoute.h"
#include "Commander/Perception/Spatial/PositionField.h"
#include "Commander/Perception/Spatial/FieldSpots.h"
#include "Commander/Perception/Values/ValueModel.h"
#include "Commander/Perception/Tanks.h"
#include "TeamBotAI.h"
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
            bool taken = false;     // field cell another member holds (FieldSpots::Spot::taken)
        };

        // Perception/Tanks.h when the party's world model is there; else a tank-role bot.
        bool IsTank(WorldModel const* world, Player* member)
        {
            if (world)
                if (PartyMemberFacts const* m = world->FindMember(member->GetObjectGuid()))
                    return Tanks::IsTank(*m);
            TeamBotAI* ai = member->AI() ? dynamic_cast<TeamBotAI*>(member->AI()) : nullptr;
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
        {
            for (Player* m : Team::Members(me))
            {
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

        // Points (header "Order"); without the facts of the bot and the enemy: tanks first, then gap.
        PartyMemberFacts const* self = world ? world->FindMember(me->GetObjectGuid()) : nullptr;
        EnemyFacts const* foeFacts = world ? world->FindEnemy(enemy->GetObjectGuid()) : nullptr;
        bool const valued = self && foeFacts;
        float const speed = std::max(1.0f, me->GetSpeed(MOVE_RUN));
        float const enemySpeed = foeFacts && foeFacts->speed > 0.0f ? foeFacts->speed : ENEMY_RUN_SPEED;

        std::vector<Candidate> candidates;
        auto const score = [&](Position3 const& p, bool toTank, Candidate& c) -> bool
        {
            float const gap = Distance2d(p, foe);
            if (!toTank && (gap < MIN_ENEMY_GAP || gap < nowGap + MIN_GAIN))
                return false;
            c.pos = p;
            if (!valued)
            {
                c.score = (toTank ? 1000.0f : 0.0f) + gap;
                return true;
            }
            float const freeSec = toTank ? TANK_HOLD_SEC : std::min(gap / enemySpeed, MAX_FREE_SEC);
            float const back = std::max(0.0f, Distance2d(p, centre) - REJOIN_YD) * PATH_FACTOR / speed;
            c.score = Value::HitLoss(*world, *foeFacts, *self, freeSec) - Value::LostAction(*world, *self, back);
            return true;
        };

        // Party members: ground near the bot's height.
        for (auto const& ally : allies)
        {
            Position3 p = ally.first;
            float const ground = map->GetHeight(p.x, p.y, here.z + MAX_HEIGHT_DIFFERENCE, true, 2.0f * MAX_HEIGHT_DIFFERENCE);
            if (ground <= INVALID_HEIGHT || std::fabs(ground - here.z) > MAX_HEIGHT_DIFFERENCE)
                continue;
            p.z = ground;
            Candidate c;
            if (score(p, ally.second, c))
                candidates.push_back(c);
        }

        // Cells of the position field around the bot (ground, idle creatures, patrols, a grid
        // walk there: Perception/Spatial/FieldSpots.h).
        PositionField const* field = world ? world->field.get() : nullptr;
        if (field && self)
        {
            FieldSpots::Query q;
            q.center = here;
            q.minYd = CELL_MIN_DISTANCE;
            q.maxYd = CELL_MAX_DISTANCE;
            q.patrolBeforeSec = Value::FightRemainingSec(*world);
            for (FieldSpots::Spot const& s : FieldSpots::Collect(*world, *self, q))
            {
                Candidate c;
                c.taken = s.taken;
                if (score(s.pos, false, c))
                    candidates.push_back(c);
            }
        }

        std::sort(candidates.begin(), candidates.end(),
                  [](Candidate const& a, Candidate const& b) { return a.score > b.score; });
        // (party members' own spots are where they stand: running to them is the point)
        float const tie = valued ? Value::LostAction(*world, *self, PositionField::CELL_YD * PATH_FACTOR / speed)
                                 : FieldSpots::TIE;
        FieldSpots::PreferFree(candidates, [](Candidate const& c) { return -c.score; },
                               [](Candidate const& c) { return c.taken; }, tie);

        uint32 checks = 0;
        for (Candidate const& c : candidates)
        {
            if (checks >= MAX_PATH_CHECKS)
                break;
            // This bot's own idle scan at the end (the field uses the farthest reach of the party).
            if (idle.Near(c.pos))
                continue;
            ++checks;
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
