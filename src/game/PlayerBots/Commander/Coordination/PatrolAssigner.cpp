#include "Commander/Coordination/PatrolAssigner.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/WorldModel.h"
#include "Commander/Perception/PackResolver.h"
#include "Commander/Perception/ProfileStore.h"
#include "Commander/Perception/CreatureProfile.h"
#include "Commander/Perception/ValueModel.h"
#include "Commander/Perception/Tanks.h"
#include "Timer.h"
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>

namespace Commander
{
    namespace
    {
        // Living members (players and bots, no pets), each shifted by (dx, dy): where they
        // stand after the tank moved that far (they keep their place around the tank).
        std::vector<Position3> Members(WorldModel const& world, float dx, float dy)
        {
            std::vector<Position3> out;
            for (PartyMemberFacts const& m : world.party)
            {
                if (m.isPet || !m.alive)
                    continue;
                Position3 p = m.pos;
                p.x += dx;
                p.y += dy;
                out.push_back(p);
            }
            return out;
        }

        // First time (seconds from the refresh) the patrol's predicted walk comes within
        // `radius` of any of `members`, -1 = not within the track.
        float FirstTouch(PatrolFacts const& p, std::vector<Position3> const& members, float radius)
        {
            for (size_t i = 0; i < p.track.size(); ++i)
                for (Position3 const& m : members)
                    if (PackResolver::Distance(p.track[i], m) <= radius)
                        return float(i) * p.trackStepSec;
            return -1.0f;
        }

        std::string NameOf(uint32 entry)
        {
            std::shared_ptr<CreatureProfile const> profile = sProfileStore.GetCreature(entry);
            return profile ? profile->name : std::string("patrol");
        }

        std::string Format(char const* fmt, ...)
        {
            char buf[256];
            va_list ap;
            va_start(ap, fmt);
            vsnprintf(buf, sizeof(buf), fmt, ap);
            va_end(ap);
            return buf;
        }

        bool Contains(std::vector<ObjectGuid> const& list, ObjectGuid guid)
        {
            return std::find(list.begin(), list.end(), guid) != list.end();
        }

        // Ring points around the tank where the patrol's walk never comes near the party moved
        // along with the tank, nearest ring first; within a ring, farthest from where the
        // patrol arrives first.
        std::vector<Position3> Spots(WorldModel const& world, PatrolFacts const& p, Position3 const& tank,
                                     Position3 const& arrivesAt, float radius)
        {
            std::vector<Position3> spots;
            float const away = std::atan2(tank.y - arrivesAt.y, tank.x - arrivesAt.x);
            for (float d : PatrolAssigner::SPOT_DISTANCES)
            {
                for (uint32 k = 0; k < PatrolAssigner::SPOT_ANGLES && spots.size() < PatrolAssigner::MAX_SPOTS; ++k)
                {
                    // 0, +1, -1, +2, -2 ... steps from straight away from the patrol.
                    int32 const step = int32((k + 1) / 2) * ((k % 2) ? 1 : -1);
                    float const angle = away + 2.0f * float(M_PI) * step / PatrolAssigner::SPOT_ANGLES;
                    Position3 s;
                    s.x = tank.x + d * std::cos(angle);
                    s.y = tank.y + d * std::sin(angle);
                    s.z = tank.z;
                    if (FirstTouch(p, Members(world, s.x - tank.x, s.y - tank.y), radius) < 0.0f)
                        spots.push_back(s);
                }
            }
            return spots;
        }

        void End(PatrolMovePlan& m)
        {
            m.active = false;
            m.spots.clear();
            m.chosen = -1;
        }
    }

    void PatrolAssigner::Clear(TeamPlan& plan) const
    {
        plan.patrolMove = PatrolMovePlan();
    }

    void PatrolAssigner::Assign(WorldModel const& world, TeamPlan& plan) const
    {
        PatrolMovePlan& m = plan.patrolMove;
        if (!world.HasPartyEnemy())
        {
            m = PatrolMovePlan();       // new fight, new judgements
            return;
        }
        // The retreat and the pull own the tank's movement.
        if (plan.retreat.active || plan.pull.phase == PullPhase::Approach || plan.pull.phase == PullPhase::Return)
        {
            End(m);
            return;
        }
        PartyMemberFacts const* tank = Tanks::Main(world, true);
        if (!tank || !tank->CanAct())
        {
            End(m);
            return;
        }

        if (m.active)
        {
            auto patrol = std::find_if(world.patrols.begin(), world.patrols.end(),
                [&m](PatrolFacts const& p) { return p.guid == m.patrol; });
            std::string const name = patrol != world.patrols.end() ? NameOf(patrol->entry) : std::string("The patrol");
            if (m.tank != tank->guid)
                End(m);
            else if (m.chosen == -2)
            {
                plan.notes.push_back("No reachable spot away from the patrol's path, fighting here.");
                End(m);
            }
            else if (m.chosen >= 0 && PackResolver::Distance(tank->pos, m.chosenSpot) <= ARRIVED_DISTANCE)
                End(m);
            // Joined the fight (now an enemy), died, or walked out of the scan.
            else if (patrol == world.patrols.end())
                End(m);
            else if (WorldTimer::getMSTimeDiff(m.sinceMs, world.refreshedAtMs) > MOVE_TIMEOUT_MS)
            {
                plan.notes.push_back(name + ": moving the fight took too long, fighting here.");
                End(m);
            }
            return;
        }

        float const remaining = Value::FightRemainingSec(world);
        float const dealt = std::max(1.0f, Value::DealtDps(world));
        std::vector<Position3> const members = Members(world, 0.0f, 0.0f);
        for (PatrolFacts const& p : world.patrols)
        {
            if (!p.aggressive || p.noAggro || p.tappedByOther || p.track.empty() || Contains(m.judged, p.guid))
                continue;
            float const radius = p.reach + ZONE_MARGIN;
            float const arrive = FirstTouch(p, members, radius);
            if (arrive < 0.0f || arrive >= remaining)
                continue;
            m.judged.push_back(p.guid);
            std::string const name = NameOf(p.entry);

            // What it costs if it joins: it hits the tank from arrival until the fight is over
            // and it is dead, and the fight lasts its kill time longer.
            float const killSec = float(p.health) / dealt;
            float const hit = p.meleeDps * (1.0f - Value::ArmorReduction(tank->armor, p.level));
            float const benefit = Value::Points(world, hit * (remaining - arrive + killSec)) +
                                  Value::FightExtension(world, killSec);

            Position3 const arrivesAt = p.track[std::min(p.track.size() - 1, size_t(arrive / p.trackStepSec))];
            std::vector<Position3> spots = Spots(world, p, tank->pos, arrivesAt, radius);
            if (spots.empty())
            {
                plan.notes.push_back(Format("%s reaches the fight in about %.0f s (fight about %.0f s left); "
                    "no spot within %.0f yd keeps away from its path, fighting here.", name.c_str(), arrive, remaining,
                    SPOT_DISTANCES[sizeof(SPOT_DISTANCES) / sizeof(float) - 1]));
                continue;
            }
            float const walkSec = PackResolver::Distance(tank->pos, spots.front()) * PATH_FACTOR / RUN_YARDS_PER_SEC;
            float const cost = Value::FightExtension(world, walkSec);
            if (benefit <= cost)
            {
                plan.notes.push_back(Format("%s reaches the fight in about %.0f s; staying (it costs %.1f, "
                    "moving %.1f).", name.c_str(), arrive, benefit, cost));
                continue;
            }

            m.active = true;
            m.patrol = p.guid;
            m.tank = tank->guid;
            m.spots = std::move(spots);
            m.arriveSec = arrive;
            m.benefit = benefit;
            m.cost = cost;
            m.sinceMs = world.refreshedAtMs;
            m.chosen = -1;
            plan.notes.push_back(Format("%s reaches the fight in about %.0f s (fight about %.0f s left): "
                "moving the fight about %.0f yd off its path (%.1f vs %.1f).", name.c_str(), arrive, remaining,
                PackResolver::Distance(tank->pos, m.spots.front()), benefit, cost));
            return;
        }
    }
}
