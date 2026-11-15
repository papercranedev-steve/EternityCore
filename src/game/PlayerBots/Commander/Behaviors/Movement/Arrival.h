/*
 * PartyBot Commander layer 3 (cerebellum) with its hands: a bot that comes back into the world
 * away from the leader (a dungeon entrance after its corpse run, Behaviors/Support/CorpseRun.h)
 * walks to him instead of being teleported (user, 2026-11-15). Later also for bots that join
 * from another map.
 *
 * Update (bot update, out of combat, in place of the 100 yd teleport and the follow):
 *   - the leader a ghost (his own corpse run): the bot waits where it is, and eats or drinks;
 *     his body lying unreleased: the bot walks to it (the resurrection there, ResurrectApproach);
 *   - the leader alive on another map or instance: the walk ends (the original teleport);
 *   - else a navmesh walk (MovePoint with pathfinding: long paths are cut, so it is made again
 *     every REPATH_MS from where the bot got to, or when the leader moved REPATH_MOVED_YD);
 *   - ends within DONE_YD of the leader (the original follow goes on), after MAX_MS, or when
 *     the bot got no PROGRESS_YD closer in STUCK_MS (the original teleport takes it there).
 * In combat it does nothing (the fight comes first); the walk goes on afterwards.
 *
 * Map thread of the bot.
 */

#ifndef MANGOS_COMMANDER_ARRIVAL_H
#define MANGOS_COMMANDER_ARRIVAL_H

#include "Common.h"
#include "Commander/Perception/WorldModel.h"

class PartyBotAI;
class Player;

namespace Commander
{
    // Per bot (PartyBotAI member). Plain data.
    struct ArrivalState
    {
        bool active = false;
        uint32 deadlineMs = 0;
        uint32 nextMoveMs = 0;
        Position3 goal;             // the leader's position of the last walk order
        float bestDist = 0.0f;      // the closest the bot got to the leader
        uint32 bestMs = 0;          // when
    };

    namespace Arrival
    {
        constexpr float DONE_YD = 20.0f;
        constexpr uint32 REPATH_MS = 2000;
        constexpr float REPATH_MOVED_YD = 5.0f;
        constexpr float PROGRESS_YD = 5.0f;
        constexpr uint32 STUCK_MS = 20000;
        constexpr uint32 MAX_MS = 5 * 60 * 1000;

        void Start(ArrivalState& state, uint32 nowMs);
        void Stop(ArrivalState& state);

        // Out of combat bot update. True: the bot is on its way or waits for the leader
        // (PartyBotAI::UpdateAI does nothing else this tick: no teleport, no follow).
        bool Update(PartyBotAI* bot, ArrivalState& state, Player* leader);
    }
}

#endif
