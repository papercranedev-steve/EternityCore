/*
 * PartyBot Commander layer 2 (situation): the player's active pull stance (PullStance.h, the
 * talk window's 战斗策略 page). The party does not wait for the player to start every fight.
 * Design: .kiro/docs/2026-11-24-pull-stance.md (workspace root).
 *
 * Only with a real player leading (a team of bots hunts on its own, Hunt/HuntAssigner), the
 * pull module on and a tank bot alive (Tanks::Main, bots only): without one it is passive.
 *
 * Dungeon / raid: a plain clear (ClearPlanner, its defaults) around the player once he stood
 * still LEADER_STILL_MS and nothing else runs; after it ended: nothing left -> once he moved
 * MOVED_YD or RETRY_MS later; any other end (deaths, a failed pull, stopped) -> once he moved
 * MOVED_YD. A boss or a pack over the limit is asked about (ClearPlan::asks) instead of skipped.
 *
 * Open world: a standing clear (ClearPlan::standing, chosenOnly) of the packs within
 * WORLD_RADIUS that PackJudge says Pull, chosen again every refresh; the nearest Ask pack is
 * asked about (PullQuestions). Nothing is pulled while he is dead, mounted, flying, resting or in
 * a battleground, nor while the main tank is told to wait, a guide, a retreat or a push runs.
 * A fight he started himself (LeaderActivityFacts::engaged) that our pull did not start makes
 * its camp one he wants (Camps.h).
 *
 * Passive again: a clear AutoClear started stops (a pull already under way is finished) and
 * the open question is dropped.
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
        // Dungeon
        uint32 const LEADER_STILL_MS = 5000;        // the player stood still this long ...
        float const LEADER_STILL_RADIUS = 2.0f;     // ... within this distance
        float const MOVED_YD = 20.0f;               // moved this far from where the last clear ended
        uint32 const RETRY_MS = 30 * 1000;          // nothing was left: look again after this long

        // Open world
        float const WORLD_RADIUS = 30.0f;           // packs this far (walk) from the player

        void Step(TeamBotAI* bot, PartyCommander& commander, uint32 nowMs);
    }
}

#endif
