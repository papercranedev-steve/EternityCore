/*
 * PartyBot Commander layer 2 (brain): patrols walking next to a pull while the fight lasts,
 * and the start of the pull they allow (rules in PullPlanner.h, "Patrols"). Used by
 * PullPlanner::Plan (reported) and RecheckBeforeApproach (waited for). Map thread.
 * Split out of PullPlanner.cpp (todo item 14), behaviour unchanged.
 */

#ifndef MANGOS_COMMANDER_PULL_PATROL_CHECK_H
#define MANGOS_COMMANDER_PULL_PATROL_CHECK_H

#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/Forecast/FightLoad.h"
#include "Commander/Perception/Forecast/PullTimeline.h"
#include "Commander/Perception/Spatial/PatrolTracker.h"
#include "Commander/Perception/WorldModel.h"
#include <map>
#include <memory>
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

        // A patrol that comes to the party while the pull lasts (PullPlanner.h, "Patrols"): it
        // is at one of the pull's places while the party is there (PullTimeline::Presence).
        struct PatrolArrival
        {
            Creature* patrol = nullptr;
            FightLoad::Foe foe;
            float clearance = 0.0f;         // max(PATROL_MIN_CLEARANCE, aggro range against the tank): Place zones
            float walkReach = 0.0f;         // aggro range against the tank + the hard line: Walk zones
            float partyReach = 0.0f;        // max(PATROL_MIN_CLEARANCE, aggro range against the leader): Party
            PatrolTracker::TrackSource source = PatrolTracker::TrackSource::Standing;
            std::vector<float> atSec;       // track sample times (from now)
            std::vector<Position3> pos;     // track sample positions
            // The pull's places, times from the tank's go (shared by every patrol of one check).
            std::shared_ptr<std::vector<PullTimeline::Zone> const> zones;
            // Sample index -> who it calls for help starting to attack there (ChooseStart fills
            // it for the samples it needs).
            std::map<int, std::vector<PatrolCall>> calls;

            // The place sample `i` is at while the party is there, for a pull started `startSec`
            // from now; null = none.
            PullTimeline::Zone const* ZoneAt(size_t i, float startSec) const
            {
                if (!zones)
                    return nullptr;
                float const t = atSec[i] - startSec;
                if (t < -0.001f)
                    return nullptr;
                for (PullTimeline::Zone const& z : *zones)
                {
                    if (t < z.fromSec - 0.001f || t > z.toSec + 0.001f)
                        continue;
                    float const r = z.kind == PullTimeline::ZoneKind::Walk ? walkReach
                                  : z.kind == PullTimeline::ZoneKind::Party ? partyReach : clearance;
                    float const dx = pos[i].x - z.center.x, dy = pos[i].y - z.center.y, dz = pos[i].z - z.center.z;
                    if (dx * dx + dy * dy + dz * dz <= r * r)
                        return &z;
                }
                return nullptr;
            }

            // The sample where it comes to the party for a pull started `startSec` from now,
            // -1 if not within the track.
            int ArrivalIndex(float startSec) const
            {
                for (size_t i = 0; i < atSec.size(); ++i)
                    if (atSec[i] + 0.001f >= startSec && ZoneAt(i, startSec))
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
            // From the tank's go to the end of the fight without the patrols: the length their
            // tracks are checked for. Arrivals (ArriveAfter) count from the go too.
            float fightSec = 0.0f;
            // The fight (FightLoad's 0 s) starts this long after the go (PullTimeline contact).
            float contactSec = 0.0f;
            // The patrol that matters most when pulled now (earliest to join), and when it joins.
            int mainIndex = -1;
            float mainArriveNow = -1.0f;
            uint32 mainCallsNow = 0;        // creatures the main patrol calls for help when pulled now
            // Not bearable now: pull now and hold one patrol when it comes (battlefield groups
            // doc 5, user 2026-11-13: "can't win, control"), tried before waiting. -1 = none.
            int holdIndex = -1;
            ObjectGuid holdBot;
            uint32 holdSpellId = 0;

            PatrolArrival const* Main() const { return mainIndex >= 0 ? &patrols[mainIndex] : nullptr; }
            PatrolArrival const* Held() const { return holdIndex >= 0 ? &patrols[holdIndex] : nullptr; }
        };

        // The only place patrols are judged for a pull (design:
        // .kiro/docs/2026-11-25-pull-patrol-timeline.md): along `pull`'s whole timeline from the
        // tank's go (PullTimeline::Presence: the tank's walk from where it stands now, the
        // pack's spots until it leaves, the open spot, the walk back, the fight place, the
        // party at pull.spot), each patrol joining when its track is at a place while the party
        // is there, calling for help where that is. The walk-in check (PullWalk), the shot spot
        // (ShotSpot, Request::patrolsTimed) and the walk guard leave patrols to this.
        // Bearable now (every patrol joining that way): now. Else now with one patrol held (holdIndex: a bot's single
        // target control on it, ControlValue::OptionsFor, ready, no fear, not the tank's, bots
        // without area spells first; held for a Hold kind's duration, a Mez until the rest of
        // the fight is over) when that is bearable. Else the earliest start within
        // `waitBudgetSec` that is bearable; none: the clearly best start
        // (PullPlanner::PATROL_WAIT_MIN_GAIN), else now.
        PatrolStart ChooseStart(WorldModel const& world, Map* map, Creature* target, Unit* tank, Unit* leader,
                                PullPlan const& pull, std::vector<ObjectGuid> const& expected, float waitBudgetSec);

        // "Patchwork Horror is next to the pull (path)" / "Patchwork Horror joins in about 12 s (path)"
        std::string DescribePatrol(PatrolStart const& s);
        // What the patrol check decided, for the report and the notes. `waiting`: "The pull
        // waits" (planned) or "Waiting" (held).
        std::string PatrolVerdict(PatrolStart const& s, char const* waiting);
    }
}

#endif
