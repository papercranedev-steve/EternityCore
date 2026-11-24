/*
 * PartyBot Commander layer 2 (situation): the player's active pull stance (PullStance.h, the
 * talk window's 战斗策略 page). The party does not wait for the player to start fights: while
 * it is idle a clear (ClearPlanner, `.partybot ai clear` with its defaults) runs around the
 * player, the tank pulls nearby packs one after another.
 *
 * A clear is started when all of these hold (TeamPlan::autoClear):
 *  - the player's stance is Active, a real player leads the party (not a team of bots: they
 *    hunt on their own, Hunt/HuntAssigner) and the pull module is on;
 *  - a tank bot is alive (Tanks::Main, bots only): without one the party is passive;
 *  - nothing else runs: no clear, push, retreat, single pull, guide, nobody told to wait
 *    somewhere (Stay.h), no fight;
 *  - the player is alive, not mounted, not on a flight, not resting (inn, city), not in a
 *    battleground, and has stood still for LEADER_STILL_MS (he is not on his way somewhere);
 *  - after a clear ended: nothing left -> once he moved MOVED_YD away or RETRY_MS later
 *    (respawns, patrols); any other end (deaths, a failed pull, stopped) -> only once he
 *    moved MOVED_YD away (the same spot would fail the same way).
 * While it runs it is a plain clear: paused while the player walks off, goes on where he stops.
 * Passive again: a clear AutoClear started stops (a pull already under way is finished).
 *
 * Runs on the map thread from PartyCommander::Update, after ClearPlanner::Step.
 */

#ifndef MANGOS_COMMANDER_AUTO_CLEAR_H
#define MANGOS_COMMANDER_AUTO_CLEAR_H

#include "Common.h"

class TeamBotAI;

namespace Commander
{
    class PartyCommander;

    namespace AutoClear
    {
        uint32 const LEADER_STILL_MS = 5000;        // the player stood still this long ...
        float const LEADER_STILL_RADIUS = 2.0f;     // ... within this distance
        float const MOVED_YD = 20.0f;               // moved this far from where the last clear ended
        uint32 const RETRY_MS = 30 * 1000;          // nothing was left: look again after this long

        void Step(TeamBotAI* bot, PartyCommander& commander, uint32 nowMs);
    }
}

#endif
