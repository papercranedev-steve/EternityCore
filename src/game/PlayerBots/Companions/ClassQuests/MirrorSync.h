/*
 * Keeps the leader's mirror quest (Shared/QuestMirror) in step with the quest a companion is
 * doing for real. One link per bot: the quest it follows, through its chain:
 *   bot does not have the quest yet  -> the leader has the placeholder ("bring it to <giver>")
 *   bot has it                       -> the leader has the mirror, counters copied from the bot
 *   bot turned it in                 -> the next quest of the chain (NextQuestInChain), or done
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
            uint32 questId = 0;     // the real quest followed; 0 = none
            uint32 shownId = 0;     // the mirror or placeholder put in the leader's log; 0 = none
            uint32 lastCheckMs = 0; // WorldTimer::getMSTime() of the last check, 0 = check now
            bool logFullSaid = false;
        };

        enum class Event : uint8
        {
            None,
            StepDone,           // the bot turned a quest of the chain in, the link went on to the next
            ChainDone,          // ... and there is no next: the link ended
            LeaderAbandoned,    // the leader took the mirror out of the log: the link ended
            BotAbandoned,       // the bot dropped the real quest: the link ended
        };

        // Starts following `questId` (shown at the next update). Replaces what was followed: call Stop first.
        void Follow(Link& link, uint32 questId);

        // Ends the link and takes its mirror out of the leader's log (leader may be null: only ends it).
        void Stop(Player* leader, Link& link);

        Event Update(Player* bot, Player* leader, Link& link);
    }
}

#endif
