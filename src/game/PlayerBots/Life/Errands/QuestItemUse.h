/*
 * Bot daily life (PlayerBots/Life), errand: use a quest item on the creature it is meant for, as
 * a player does ("use the Taming Rod to tame a Large Crag Boar"). Which item on which creature is
 * the world database's: item_required_target (ObjectMgr::GetItemRequiredTargetMapBounds) of an
 * item an incomplete quest in the bot's log gave (SrcItemId) or asks for (ReqItemId), creature
 * targets alive or dead as the row says. The bot walks into the spell's range of the nearest
 * fitting creature near the anchor (the leader) and uses the item through the client's path
 * (Shared/ItemUse). A channel (the taming rod) goes on by itself; whatever the spell does for the
 * quest (taming completes it) is the core's.
 * Design: .kiro/docs/2026-11-18-companion-class-quests.md, stage C6 (workspace root).
 *
 * Same gate as the other errands (ErrandCommon::MayRun: out of combat), but no safe-walk check:
 * the target is an enemy in a field of them, and the player brought the bot there for it. A
 * creature it used the item on, or could not reach or use it on, is not tried again for DONE_KEEP
 * creatures.
 * Map thread.
 */

#ifndef MANGOS_LIFE_QUEST_ITEM_USE_H
#define MANGOS_LIFE_QUEST_ITEM_USE_H

#include "Common.h"
#include "ObjectGuid.h"
#include "Life/Errands/ErrandCommon.h"

class Player;

namespace Life
{
    struct QuestItemUseState
    {
        ObjectGuid target;
        uint32 itemId = 0;
        uint32 questId = 0;
        uint32 startedMs = 0;
        uint32 nextMoveMs = 0;
        uint32 nextSearchMs = 0;
        Errands::DoneList done;
    };

    namespace QuestItemUse
    {
        constexpr float SEARCH_YD = 40.0f;
        constexpr float ANCHOR_YD = 50.0f;
        constexpr uint32 SEARCH_MS = 2000;
        constexpr uint32 REPATH_MS = 1000;
        constexpr uint32 GIVE_UP_MS = 25000;
        constexpr size_t DONE_KEEP = 8;

        // True: the bot walks to a target or uses the item this tick.
        bool Update(Player* me, QuestItemUseState& state, Player const* anchor);
        void Stop(Player* me, QuestItemUseState& state);
    }
}

#endif
