/*
 * Mirror quests: a copy of a bot's quest in the player's quest log, so the player can follow it.
 * A mirror is its own quest_template row (id = real quest + OFFSET) that nobody gives and nobody
 * can turn in; its objectives are counters with their own text, and only the server sets them
 * (from the bot's progress). These are the hands: put it in the log, set a counter, take it out.
 *
 * Design: .kiro/docs/2026-11-18-companion-class-quests.md §2 (workspace root). Stage C1: one
 * hand-made mirror (sql/migrations/20261118220000_world.sql) and `.partybot mirror` to check the
 * client; C2 adds the mirror flag (no progress from the player's own kills) and generates them.
 */

#ifndef MANGOS_BOTSHARED_QUEST_MIRROR_H
#define MANGOS_BOTSHARED_QUEST_MIRROR_H

#include "Common.h"
#include "ObjectGuid.h"

class Player;

namespace BotShared
{
    namespace QuestMirror
    {
        uint32 const OFFSET = 100000;

        enum class ShowResult { Shown, AlreadyShown, NoQuest, LogFull };

        // Into the quest log, as an accepted shared quest (no giver, no accept scripts).
        ShowResult Show(Player* player, uint32 mirrorId);

        // Counter `objective` (0..3) to `count` (capped at what it needs), with the client's
        // "x: n/m" line. All counters full: the mirror shows as complete. False: not in the log, or
        // the objective is not a counter.
        bool SetCount(Player* player, uint32 mirrorId, uint32 objective, uint32 count);

        // Out of the quest log, not rewarded (as if abandoned).
        bool Remove(Player* player, uint32 mirrorId);
    }
}

#endif
