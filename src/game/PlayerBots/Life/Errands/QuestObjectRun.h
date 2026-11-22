/*
 * Bot daily life (PlayerBots/Life), errand: use the quest objects its own quests need, as a
 * player of the group does (each player clicks the quest object for himself): a quest objective
 * "use this" (Player::HasQuestForGO), a chest holding a quest item it still needs
 * (GameObject::ActivateToQuest), a goober of an active quest. Soul companions doc §4.2 (S2).
 * Not plain treasure chests: those are the player's to open.
 *
 * Opened like a client (Shared/ObjectUse.h): the use handler, the chest's loot, or the lock's
 * spell (Opening, a key, Pick Lock) with its cast time; what the chest gives is taken like a
 * corpse (Shared/LootFacts::TakeOpenLoot) and upgrades are put on.
 *
 * Same rules as LootRun: out of combat, nobody of the group fighting, not mounted, standing,
 * within SEARCH_YD of the bot and ANCHOR_YD of the anchor, a safe walk. An object it used is not
 * tried again for FORGET_MS (it respawns under the same guid); one it could not reach (no closer
 * for STUCK_MS, or not there in GIVE_UP_MS) or cannot open, for UNREACHABLE_FORGET_MS.
 *
 * Map thread of the bot.
 */

#ifndef MANGOS_LIFE_QUEST_OBJECT_RUN_H
#define MANGOS_LIFE_QUEST_OBJECT_RUN_H

#include "Common.h"
#include "ObjectGuid.h"
#include "Life/Errands/ErrandCommon.h"
#include <set>

class Player;

namespace Life
{
    // Per bot (a member of its AI). Plain data.
    struct QuestObjectRunState
    {
        ObjectGuid object;          // the object walked to / being opened
        uint32 startedMs = 0;
        uint32 nextMoveMs = 0;
        uint32 nextSearchMs = 0;
        float closestYd = 0.0f;     // nearest the bot got to the object on this walk
        uint32 closerMs = 0;        // when it last got PROGRESS_YD closer
        bool opening = false;       // the use was started (a cast may be running)
        std::set<ObjectGuid> bagsBefore;    // to put on what the object gave
        Errands::DoneList done;         // used: forgotten after FORGET_MS
        Errands::DoneList unreachable;  // no safe walk, stuck, cannot open: UNREACHABLE_FORGET_MS
    };

    namespace QuestObjectRun
    {
        constexpr float SEARCH_YD = 30.0f;
        constexpr float ANCHOR_YD = 35.0f;
        constexpr float MAX_PATH_DETOUR = 2.5f;
        constexpr float APPROACH_YD = 2.5f;     // in front of the object (it may sit in a wall)
        constexpr uint32 SEARCH_MS = 2000;
        constexpr uint32 REPATH_MS = 2000;
        constexpr uint32 GIVE_UP_MS = 20000;
        // Not PROGRESS_YD closer for STUCK_MS: the walk is stuck (the spot inside a model, a
        // path that does not get there), give the object up instead of standing still.
        constexpr uint32 STUCK_MS = 4000;
        constexpr float PROGRESS_YD = 0.5f;
        constexpr size_t DONE_KEEP = 16;
        // A used object respawns under the same guid (a cactus apple, a crate): the mark is
        // forgotten after this, so the respawned one is used again.
        constexpr uint32 FORGET_MS = 60000;
        // One it could not reach or open is tried again after this (from elsewhere it may work).
        constexpr uint32 UNREACHABLE_FORGET_MS = 300000;

        // `anchor`: objects near it only (the party leader); null = near the bot.
        // True: the bot walks to an object or opens one this tick.
        bool Update(Player* me, QuestObjectRunState& state, Player const* anchor);
        void Stop(Player* me, QuestObjectRunState& state);
    }
}

#endif
