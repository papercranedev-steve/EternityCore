/*
 * PartyBot Commander layer 2 (situation): the player's active pull stance (PullStance.h, the
 * talk window's 战斗策略 page). The party does not wait for the player to start every fight.
 * Design: .kiro/docs/2026-11-24-pull-stance.md (workspace root).
 *
 * Only with a real player leading (a team of bots hunts on its own, Hunt/HuntAssigner), the
 * pull module on and a tank bot alive (Tanks::Main, bots only): without one it is passive.
 *
 * Dungeon / raid: a clear of the packs in front of the player (ClearPlan::frontOnly: nothing
 * in between, in his sight or a short walk (ClearPlanner::FRONT_UNSEEN_WALK_YD), at most
 * FRONT_WALK_YD walk) once he stood still LEADER_STILL_MS
 * and nothing else runs; after it ended: nothing left -> once he moved MOVED_YD or RETRY_MS
 * later; any other end (deaths, a failed pull, stopped) -> once he moved MOVED_YD. No count
 * limit: a boss or a pack the fight estimate says is too hard (PullPlan::hard) is asked about
 * (ClearPlan::asks) instead of skipped.
 * Bosses (user 2026-11-28): planned like any pack and asked about when the tank would go
 * (everyone ready, nothing else in front); no other pack is pulled while that question waits.
 * A boss in front not asked about yet starts a clear without the MOVED_YD / RETRY_MS wait, and
 * at once (no settle) when he selects it; once per boss (AutoClearPlan::bossStartFor), again
 * after a "wait" (BossWaits.h). Selecting it never pulls it: it is only asked sooner.
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
        uint32 const LEADER_STILL_MS = 3000;        // the player stayed this long ...
        float const LEADER_STILL_RADIUS = 8.0f;     // ... within this distance (strafing in place counts, running does not)
        float const MOVED_YD = 20.0f;               // moved this far from where the last clear ended
        uint32 const RETRY_MS = 30 * 1000;          // nothing was left: look again after this long
        float const FRONT_WALK_YD = 100.0f;         // safety cap of the packs in front (walk)

        // Open world
        float const WORLD_RADIUS = 30.0f;           // packs this far (walk) from the player

        void Step(TeamBotAI* bot, PartyCommander& commander, uint32 nowMs);
    }
}

#endif
