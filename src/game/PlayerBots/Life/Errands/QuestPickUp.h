/*
 * Bot daily life (PlayerBots/Life), errand: take a quest from its giver, as a player does: a
 * creature or gameobject near the anchor (the leader) that gives one of the `wanted` quests is
 * walked to and the quest accepted through the client's own handler
 * (WorldSession::HandleQuestgiverAcceptQuestOpcode: every rule applies, and the giver's accept
 * scripts run, which is right for a quest only this bot takes). Which quests are wanted is the
 * caller's (a companion's class quest chain, Companions/ClassQuests/ClassQuestFlow).
 * Design: .kiro/docs/2026-11-18-companion-class-quests.md, stage C5 (workspace root).
 *
 * Same gate and limits as the turn-in errand (QuestTurnIn.h). Map thread of the bot.
 */

#ifndef MANGOS_LIFE_QUEST_PICK_UP_H
#define MANGOS_LIFE_QUEST_PICK_UP_H

#include "Common.h"
#include "ObjectGuid.h"
#include <deque>
#include <vector>

class Player;

namespace Life
{
    // Per bot (a member of its AI). Plain data.
    struct QuestPickUpState
    {
        ObjectGuid giver;           // creature or gameobject
        uint32 questId = 0;
        uint32 startedMs = 0;
        uint32 nextMoveMs = 0;
        uint32 nextSearchMs = 0;
        std::deque<ObjectGuid> done;
    };

    namespace QuestPickUp
    {
        constexpr float SEARCH_YD = 30.0f;
        constexpr float ANCHOR_YD = 35.0f;
        constexpr float MAX_PATH_DETOUR = 2.5f;
        constexpr uint32 SEARCH_MS = 2000;
        constexpr uint32 REPATH_MS = 2000;
        constexpr uint32 GIVE_UP_MS = 20000;
        constexpr size_t DONE_KEEP = 8;

        // `wanted`: quests to take (not in the log yet). True: the bot walks to a giver this tick.
        bool Update(Player* me, QuestPickUpState& state, Player const* anchor, std::vector<uint32> const& wanted);
        void Stop(Player* me, QuestPickUpState& state);
    }
}

#endif
