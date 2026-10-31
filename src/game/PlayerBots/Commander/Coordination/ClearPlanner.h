/*
 * PartyBot Commander layer 2: `.partybot ai clear` (step 6, first part of pacing control).
 * Design: .kiro/docs/2026-11-01-pull-planning.md section 5
 *
 * Loop: fight over -> short pause -> pick the nearest hostile pack in range that is not
 * too hard -> PullPlanner::Plan (waits until everyone is ready) -> pull -> repeat.
 * Stops when nothing is left, the leader moves away, someone dies, or a pull is cancelled.
 *
 * Runs on the map thread from PartyCommander::Update (needs live objects for planning).
 */

#ifndef MANGOS_COMMANDER_CLEAR_PLANNER_H
#define MANGOS_COMMANDER_CLEAR_PLANNER_H

#include "Common.h"
#include <string>

class PartyBotAI;
class Player;

namespace Commander
{
    class PartyCommander;

    namespace ClearPlanner
    {
        uint32 const PAUSE_AFTER_FIGHT_MS = 3000;
        float const MAX_LEADER_DRIFT = 20.0f;      // leader moving further than this from the start stops it
        uint32 const MAX_LEVEL_ABOVE_LEADER = 3;

        void Start(Player* leader, PartyCommander& commander, float radius, uint32 maxMobs);
        void Stop(PartyCommander& commander, std::string const& why);
        void Step(PartyBotAI* bot, PartyCommander& commander, uint32 nowMs);
    }
}

#endif
