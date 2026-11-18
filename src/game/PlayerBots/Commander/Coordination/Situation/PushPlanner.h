/*
 * PartyBot Commander layer 2 (situation): `.partybot ai push`, the tank leads the party through
 * the dungeon on its own; the player only follows (or does whatever he likes, see below).
 *
 * Loop (TeamPlan::push):
 *  Clear    a clear (ClearPlanner, not following the player) runs from where the tank stands;
 *           it pulls every pack in range one after another. Ended with nothing left: Advance.
 *           Ended any other way (a pull failed, deaths, no tank): the push stops.
 *  Advance  the tank is the party's guide (Guide.h): the others follow it. Goal: the hostile
 *           spawn still alive (SpawnTargets) with the shortest navmesh walk from the tank among
 *           the nearest few; bosses are never goals. Walked a leg of at most LEG_YD at a time
 *           (AdvanceRoute), stopping before the first idle creature that would see it; that
 *           pack is the next clear. A fight starting on the way is a clear too.
 *
 * Other roles may fail (ai-architecture rules): before each leg everyone alive must be within
 * GATHER_YD of the tank and nobody resting, the player too: a player who stays behind stops
 * the party, it never walks off without him. A dead member, the player leaving the dungeon, a
 * retreat, a changed guide or a blocker the clear will not pull (a boss, too many mobs) stop
 * the push with a note; pull it yourself and run the command again.
 *
 * Runs on the map thread from PartyCommander::Update, after ClearPlanner::Step. Start / Stop
 * also from the GM command (world thread, maps idle).
 */

#ifndef MANGOS_COMMANDER_PUSH_PLANNER_H
#define MANGOS_COMMANDER_PUSH_PLANNER_H

#include "Common.h"
#include <string>
#include <vector>

class Map;
class TeamBotAI;
class Player;

namespace Commander
{
    class PartyCommander;

    namespace PushPlanner
    {
        float const LEG_YD = 35.0f;             // longest leg
        float const MIN_STEP_YD = 3.0f;         // a shorter leg is not walked: blocked here, or at the goal
        float const ARRIVED_YD = 2.5f;          // at the leg's end
        float const GATHER_YD = 25.0f;          // everyone this close to the tank before a leg
        uint32 const STUCK_MS = 6000;           // not moved 1 yd for this long while walking a leg
        uint32 const MAX_GOAL_FAILURES = 2;     // legs that did not arrive before a goal is given up
        uint32 const GOAL_CANDIDATES = 8;       // nearest goals (straight) whose walks are compared
        uint32 const MAX_GOAL_PATHS = 24;       // at most this many paths per goal choice

        bool Start(Player* leader, PartyCommander& commander, float radius, uint32 maxMobs,
                   std::vector<std::string>& report);
        void Stop(PartyCommander& commander, Map* map, std::string const& why);
        void Step(TeamBotAI* bot, PartyCommander& commander, uint32 nowMs);
    }
}

#endif
