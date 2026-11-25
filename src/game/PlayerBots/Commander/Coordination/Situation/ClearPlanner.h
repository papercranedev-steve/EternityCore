/*
 * PartyBot Commander layer 2: `.partybot ai clear` (step 6, first part of pacing control).
 * Design: .kiro/docs/2026-11-01-pull-planning.md section 5
 *
 * Loop: fight over -> pause (at least 3 s, longer while the player still has corpses to loot)
 * -> wait for dead members to be resurrected -> pick the next hostile pack in range that is
 * not too hard (PullOrder: open way first, nearest navmesh walk from the leader first,
 * PackRoutes.h; range = the walk, packs with no walkable way are passed over) -> PullPlanner::Plan (waits until everyone is ready) -> predict again and wait
 * for patrols when the tank is about to go (PullPlanner::RecheckBeforeApproach) -> pull -> repeat.
 *
 * Pauses while the leader is away from the start (resumes where the leader settles); a pull
 * the tank already started is finished first.
 * A pull interrupted by another fight (player attacked, patrol) does not stop it.
 * Stops when nothing is left, there is no living tank, a pull fails, somebody is not
 * resurrected in time, or somebody dies a second time (members already dead at the start
 * are waited for but do not count as a death).
 * Packs whose hostile members are all stealthed are passed over each round, not skipped.
 *
 * A standing clear (the pull stance in the open world, Pull/AutoClear.h) never ends with nothing
 * left, follows the leader, calls a pull off when he walks away from the pack before the tank
 * got there, forgets its skips every FORGET_MS and counts a death DEATH_MEMORY_MS.
 *
 * Runs on the map thread from PartyCommander::Update (needs live objects for planning).
 * Step also runs RecheckBeforeApproach for a single `.partybot ai pull` (no clear running).
 */

#ifndef MANGOS_COMMANDER_CLEAR_PLANNER_H
#define MANGOS_COMMANDER_CLEAR_PLANNER_H

#include "Common.h"
#include "Commander/Coordination/TeamPlan.h"
#include <string>

class TeamBotAI;
class Player;

namespace Commander
{
    class PartyCommander;

    namespace ClearPlanner
    {
        uint32 const PAUSE_AFTER_FIGHT_MS = 3000;   // minimum pause between fights
        // Then wait while a player of the party still has loot on a corpse nearby that is worth
        // taking (money, own quest items, items above gray quality); gray-only corpses are ignored.
        uint32 const LOOT_WAIT_MAX_MS = 15000;      // counted from the end of the fight
        float const LOOT_RADIUS = 30.0f;            // corpses this close to the leader

        float const MAX_LEADER_DRIFT = 20.0f;       // leader further than this from the start: pause (a pull under way is finished)
        uint32 const LEADER_STILL_MS = 3000;        // leader staying this long ...
        float const LEADER_STILL_RADIUS = 8.0f;     // ... within this distance (strafing in place counts, running does not): resume from there

        uint32 const REVIVE_WAIT_MS = 60 * 1000;    // after a fight with deaths, wait this long for resurrection
        uint32 const MAX_DEATH_EVENTS = 2;          // fights with a death before the clear stops

        // Prediction again when the tank is about to go; too hard (or over the limit) this long:
        // skip the pack, or ask about it (ClearPlan::asks).
        // (Patrol waiting has its own limit, PullPlanner::PATROL_WAIT_MAX_MS.)
        uint32 const REPREDICT_WAIT_MS = 30 * 1000;

        uint32 const MAX_LEVEL_ABOVE_LEADER = 3;

        // ClearPlan::frontOnly: a pack out of the leader's sight still counts as in front when
        // the navmesh walk to it is this short and passes no other creature (user 2026-11-25:
        // about the next one or two cells of a prison corridor).
        float const FRONT_UNSEEN_WALK_YD = 40.0f;

        // A standing clear (ClearPlan::standing)
        uint32 const FORGET_MS = 3 * MINUTE * IN_MILLISECONDS;          // skips and notes kept this long
        uint32 const DEATH_MEMORY_MS = 5 * MINUTE * IN_MILLISECONDS;    // a death counts this long

        // `anchor`: where the clear is centred (the leader; the guide in the push mode).
        // `followsLeader`: pause while the leader walks away (false: the push planner moves the party).
        void Start(Player* anchor, PartyCommander& commander, float radius, uint32 maxMobs, bool followsLeader = true);
        // `end`: why (TeamPlan ClearEnd), read by the push mode.
        void Stop(PartyCommander& commander, std::string const& why, ClearEnd end = ClearEnd::Failed);
        void Step(TeamBotAI* bot, PartyCommander& commander, uint32 nowMs);
    }
}

#endif
