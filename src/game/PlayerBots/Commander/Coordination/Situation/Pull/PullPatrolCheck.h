/*
 * PartyBot Commander layer 2 (brain): patrols walking next to a pull while the fight lasts,
 * and the start of the pull they allow (rules in PullPlanner.h, "Patrols"). Used by
 * PullPlanner::Plan (reported) and RecheckBeforeApproach (waited for). Map thread.
 * Split out of PullPlanner.cpp (todo item 14), behaviour unchanged.
 */

#ifndef MANGOS_COMMANDER_PULL_PATROL_CHECK_H
#define MANGOS_COMMANDER_PULL_PATROL_CHECK_H

#include "Commander/Perception/Forecast/FightLoad.h"
#include "Commander/Perception/Spatial/PatrolTracker.h"
#include "Commander/Perception/WorldModel.h"
#include <map>
#include <string>
#include <vector>

class Creature;
class Unit;
class Map;

namespace Commander
{
    namespace PullDetail
    {
        // A creature a patrol calls for help when it starts attacking next to the pull.
        struct PatrolCall
        {
            ObjectGuid guid;
            FightLoad::Foe foe;             // arriveSec: after the patrol's own arrival
        };

        // A patrol that walks next to the pull while the fight lasts (PullPlanner.h, "Patrols").
        struct PatrolArrival
        {
            Creature* patrol = nullptr;
            FightLoad::Foe foe;
            float clearance = 0.0f;         // max(PATROL_MIN_CLEARANCE, aggro range) used for the zones
            PatrolTracker::TrackSource source = PatrolTracker::TrackSource::Standing;
            std::vector<float> atSec;       // track sample times
            std::vector<bool> inside;       // the sample is within a zone (next to the pull or the fight spot)
            std::vector<Position3> pos;     // track sample positions
            // Sample index -> who it calls for help starting to attack there (ChooseStart fills
            // it for the samples it needs).
            std::map<int, std::vector<PatrolCall>> calls;

            // The sample where it is next to the pull for a pull started `startSec` from now,
            // -1 if not within the track.
            int ArrivalIndex(float startSec) const
            {
                for (size_t i = 0; i < atSec.size(); ++i)
                    if (atSec[i] + 0.001f >= startSec && inside[i])
                        return int(i);
                return -1;
            }
            // Seconds after a pull started `startSec` from now until it is next to the pull,
            // < 0 if not within the track.
            float ArriveAfter(float startSec) const
            {
                int const i = ArrivalIndex(startSec);
                return i < 0 ? -1.0f : atSec[i] - startSec;
            }
        };

        // The start of the pull (seconds from now) the patrols around it allow (ChooseStart).
        struct PatrolStart
        {
            bool considered = false;        // a patrol comes next to the pull while it lasts
            bool hasTank = true;
            float startSec = 0.0f;          // 0 = pull now
            FightLoad::Result now;          // pulled now, every patrol joining when its walk brings it
            FightLoad::Result atStart;      // pulled at startSec
            bool bearableNow = false;
            bool bearableAtStart = false;
            std::vector<PatrolArrival> patrols;
            float fightSec = 0.0f;          // without the patrols, the length their tracks are checked for
            // The patrol that matters most when pulled now (earliest to join), and when it joins.
            int mainIndex = -1;
            float mainArriveNow = -1.0f;
            uint32 mainCallsNow = 0;        // creatures the main patrol calls for help when pulled now

            PatrolArrival const* Main() const { return mainIndex >= 0 ? &patrols[mainIndex] : nullptr; }
        };

        // The earliest start within `waitBudgetSec` the fight is bearable with every patrol
        // joining when its walk brings it next to the pull (or the fight spot `spot`); none:
        // the clearly best start (PullPlanner::PATROL_WAIT_MIN_GAIN), else now.
        PatrolStart ChooseStart(WorldModel const& world, Map* map, Creature* target, Unit* tank, Unit* leader,
                                Position3 const& spot, std::vector<ObjectGuid> const& expected, float waitBudgetSec);

        // "Patchwork Horror is next to the pull (path)" / "Patchwork Horror joins in about 12 s (path)"
        std::string DescribePatrol(PatrolStart const& s);
        // What the patrol check decided, for the report and the notes. `waiting`: "The pull
        // waits" (planned) or "Waiting" (held).
        std::string PatrolVerdict(PatrolStart const& s, char const* waiting);
    }
}

#endif
