/*
 * Mirror quests: a copy of a bot's quest in the player's quest log, so the player can follow it.
 * Nobody gives a mirror and nobody can turn it in; it has no rewards and no class or race limit.
 * Its objectives are counters with their own text that only the server sets, from the bot's
 * progress (QUEST_SPECIAL_FLAG_MIRROR: the player's own kills, casts and talks do not count).
 *
 * Two mirrors per class quest, made in code at startup (BuildAll, ObjectMgr::AddCodeQuest), with
 * the real quest's texts in every loaded locale:
 *   MirrorId(q)      = q + 100000: the quest itself; counters = CountersOf(q)
 *   PlaceholderId(q) = q + 200000: "bring your companion to <giver>", before the bot has it
 *
 * Design: .kiro/docs/2026-11-18-companion-class-quests.md §2 (workspace root).
 */

#ifndef MANGOS_BOTSHARED_QUEST_MIRROR_H
#define MANGOS_BOTSHARED_QUEST_MIRROR_H

#include "Common.h"
#include <string>
#include <vector>

class Player;
class Quest;

namespace BotShared
{
    namespace QuestMirror
    {
        uint32 const MIRROR_OFFSET = 100000;
        uint32 const PLACEHOLDER_OFFSET = 200000;

        inline uint32 MirrorId(uint32 questId) { return questId + MIRROR_OFFSET; }
        inline uint32 PlaceholderId(uint32 questId) { return questId + PLACEHOLDER_OFFSET; }

        // One counter of a mirror: which part of the real quest it follows.
        struct Counter
        {
            enum class Kind : uint8
            {
                CreatureOrGO,   // ReqCreatureOrGOId[index]: kill, talk, use
                Item,           // ReqItemId[index]
                Done,           // the quest has no counted objective: 1 when the quest is complete
            };
            Kind kind = Kind::Done;
            uint8 index = 0;
            uint32 needed = 1;  // at most 63 (6 bits in the quest log field)
        };

        // The counters of `real`'s mirror, in this order: its creature / gameobject objectives,
        // then its item objectives, at most 4 (QUEST_OBJECTIVES_COUNT). None: one Done counter.
        // The mirror's objective i is counter i.
        std::vector<Counter> CountersOf(Quest const* real);

        // How far `bot` is on counter `counter` of its quest `real` (complete: all full).
        uint32 Progress(Player const* bot, Quest const* real, Counter const& counter);

        // Startup, after quests and their locales are loaded (PlayerBotMgr::Load): mirrors and
        // placeholders of all class quests (RequiredClasses). Returns how many quests it made.
        uint32 BuildAll();

        // A mirror or placeholder of a quest that has one.
        bool HasMirror(uint32 questId);

        enum class ShowResult { Shown, AlreadyShown, NoQuest, LogFull };

        // Into the quest log, as an accepted shared quest (no giver, no accept scripts).
        ShowResult Show(Player* player, uint32 mirrorId);

        // In the player's quest log (incomplete or complete).
        bool IsShown(Player const* player, uint32 mirrorId);

        // Counter `objective` (0..3) to `count` (capped at what it needs), with the client's
        // "x: n/m" line when it changes. All counters full: the mirror shows as complete; less again:
        // incomplete. False: not in the log, or the objective is not a counter.
        bool SetCount(Player* player, uint32 mirrorId, uint32 objective, uint32 count);

        // Out of the quest log, not rewarded (as if abandoned).
        bool Remove(Player* player, uint32 mirrorId);

        // A flag on the player's map and minimap at x, y of the player's map, like a guard's
        // directions (SMSG_GOSSIP_POI): "[Companion] <name>". One at a time: the next flag (also a
        // guard's) replaces it; there is no packet to take it away.
        void ShowPoint(Player* player, float x, float y, std::string const& name);

        // Makes the mirror quests (QuestDef.h: a friend of Quest, it sets the protected fields).
        class Builder
        {
        public:
            static uint32 BuildAll();
        };
    }
}

#endif
