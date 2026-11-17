/*
 * Bot daily life (PlayerBots/Life), errand: walk to a friendly NPC near the party and do some
 * business there (a task, INpcTask): sell and buy at a vendor, repair at an armorer, learn at
 * the class trainer. Soul companions doc §4.4 (stage S4).
 *
 * This is the "while we stand here anyway" visit: an NPC within SEARCH_YD of the bot and
 * ANCHOR_YD of the anchor (the leader) the task has business with. Walking far for an errand
 * ("wait for me, I go to the trainer") needs the leader's consent: stage S5, the request system.
 *
 * At the NPC the bot stands for DWELL_MS (a player browses the window too), then the task
 * runs once. An NPC is not visited again for RETRY_MS (looked at, unreachable, or done).
 * Same gate as the other errands (ErrandCommon::MayRun), safe walk.
 *
 * Map thread of the bot.
 */

#ifndef MANGOS_LIFE_NPC_VISIT_H
#define MANGOS_LIFE_NPC_VISIT_H

#include "Common.h"
#include "ObjectGuid.h"
#include <deque>
#include <utility>

class Creature;
class Player;

namespace Life
{
    class INpcTask
    {
    public:
        virtual ~INpcTask() = default;
        // Short name for logs.
        virtual char const* Name() const = 0;
        // Business with this NPC now (its flags, the bot's needs and money). Cheap checks first:
        // asked for every friendly NPC near the bot.
        virtual bool Wants(Player* me, Creature* npc) = 0;
        // At the NPC (in interaction distance): do the business.
        virtual void Do(Player* me, Creature* npc) = 0;
    };

    // Per bot and task (a member of its AI). Plain data.
    struct NpcVisitState
    {
        ObjectGuid npc;
        uint32 startedMs = 0;
        uint32 nextMoveMs = 0;
        uint32 nextSearchMs = 0;
        uint32 arrivedMs = 0;       // standing at the NPC since, 0 = not there yet
        std::deque<std::pair<ObjectGuid, uint32>> visited;  // NPC, when it may be visited again
    };

    namespace NpcVisit
    {
        constexpr float SEARCH_YD = 30.0f;
        constexpr float ANCHOR_YD = 35.0f;
        constexpr float MAX_PATH_DETOUR = 2.5f;
        constexpr uint32 SEARCH_MS = 3000;
        constexpr uint32 REPATH_MS = 2000;
        constexpr uint32 GIVE_UP_MS = 20000;
        constexpr uint32 DWELL_MS = 2500;
        constexpr uint32 RETRY_MS = 5 * MINUTE * IN_MILLISECONDS;
        constexpr size_t VISITED_KEEP = 16;

        // `anchor`: NPCs near it only (the party leader); null = near the bot.
        // True: the bot walks to an NPC or stands at it this tick.
        bool Update(Player* me, NpcVisitState& state, Player const* anchor, INpcTask& task);
        void Stop(Player* me, NpcVisitState& state);
    }
}

#endif
