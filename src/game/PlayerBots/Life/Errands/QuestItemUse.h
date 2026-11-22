/*
 * Bot daily life (PlayerBots/Life), errand: use a quest item where it is meant to be used, as a
 * player does. The items: those an incomplete quest in the bot's log gave (SrcItemId) or asks
 * for (ReqItemId), not of quests left to the player (Shared/QuestOverrides: no_item_use, a boss
 * summon or an event start). Where, from the world database:
 *  - OnCreature: item_required_target (ObjectMgr::GetItemRequiredTargetMapBounds), creature
 *    targets alive or dead as the row says ("use the Taming Rod on a Large Crag Boar");
 *  - NearFocus: the item's spell needs a spell focus (RequiresSpellFocus): next to a
 *    GAMEOBJECT_TYPE_SPELL_FOCUS of that id, within its distance (a beacon at a watchtower);
 *  - NearScriptTarget: the spell picks a spell_script_target near the caster itself
 *    (TARGET_UNIT / GAMEOBJECT_SCRIPT_NEAR_CASTER): within the spell's range of one (Umi's yeti
 *    next to a goblin, the vine healed in Maraudon).
 * The bot walks to the nearest fitting target near the anchor (the leader) and uses the item
 * through the client's path (Shared/ItemUse). A channel (the taming rod) goes on by itself;
 * whatever the spell does for the quest is the core's.
 * Design: .kiro/docs/2026-11-18-companion-class-quests.md, stage C6 (workspace root).
 *
 * Same gate as the other errands (ErrandCommon::MayRun: out of combat), but no safe-walk check:
 * the target is an enemy in a field of them, and the player brought the bot there for it. A
 * target it used the item at, or could not reach or use it at, is not tried again for DONE_KEEP
 * targets.
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
        enum class Mode : uint8 { OnCreature, NearFocus, NearScriptTarget };

        ObjectGuid target;          // a creature or an object
        uint32 itemId = 0;
        uint32 questId = 0;
        Mode mode = Mode::OnCreature;
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
