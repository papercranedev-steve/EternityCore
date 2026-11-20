/*
 * Keeps the leader's mirror quest (Shared/QuestMirror) in step with the quest a companion is
 * doing for real. One link per bot: the quest it follows, through its chain:
 *   bot does not have the quest yet  -> the leader has the placeholder ("bring it to <giver>")
 *   bot has it                       -> the leader has the mirror, counters copied from the bot
 *   bot turned it in                 -> the next quest of the chain (NextQuestInChain), or done
 * And a flag on the leader's map where to go now (nearest spawn, Shared/QuestPlaces): the giver
 * before the bot has the quest, then what the first unfinished objective needs, then who takes
 * it back once the bot's quest is complete.
 * Checked once a second, from the bot's AI (map thread), only while the leader is on the bot's map.
 * What the events mean (help accepted, declined, a step done) is the class quest flow's (stage C4).
 * Design: .kiro/docs/2026-11-18-companion-class-quests.md §2 (workspace root).
 */

#ifndef MANGOS_COMPANIONS_MIRROR_SYNC_H
#define MANGOS_COMPANIONS_MIRROR_SYNC_H

#include "Common.h"

class Player;

namespace Companions
{
    namespace MirrorSync
    {
        struct Link
        {
            uint32 chainId = 0;     // Shared/ClassQuestChains id the steps come from; 0 = NextQuestInChain
            uint32 questId = 0;     // the real quest followed; 0 = none
            uint32 shownId = 0;     // the mirror or placeholder put in the leader's log; 0 = none
            uint32 lastCheckMs = 0; // WorldTimer::getMSTime() of the last check, 0 = check now
            bool logFullSaid = false;
            // The flag on the leader's map (Shared/QuestPlaces): what it marks, and where the
            // leader stood when it was picked (the nearest spawn is picked again after a walk).
            uint32 flagEntry = 0;   // 0 = none sent
            bool flagGameObject = false;
            uint32 flagMapId = 0;
            float flagX = 0.0f, flagY = 0.0f;
            float flagFromX = 0.0f, flagFromY = 0.0f;
        };

        enum class Event : uint8
        {
            None,
            StepDone,           // the bot turned a quest of the chain in, the link went on to the next
            ChainDone,          // ... and there is no next: the link ended
            LeaderAbandoned,    // the leader took the mirror out of the log: the link ended
            BotAbandoned,       // the bot dropped the real quest: the link ended
        };

        // Starts following `questId` (shown at the next update), then the steps after it in chain
        // `chainId` (0: by NextQuestInChain). Replaces what was followed: call Stop first.
        void Follow(Link& link, uint32 questId, uint32 chainId = 0);

        // Ends the link and takes its mirror out of the leader's log (leader may be null: only ends it).
        void Stop(Player* leader, Link& link);

        Event Update(Player* bot, Player* leader, Link& link);
    }
}

#endif
