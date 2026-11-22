/*
 * Bot daily life (PlayerBots/Life), errand: take a quest from its giver, as a player does: a
 * creature or gameobject that gives one of the `wanted` quests is walked to (GiverVisit.h: its
 * spot in front of the giver, a moment talking) and the quest taken. Which quests are wanted is
 * the caller's:
 *  - its own (a companion's class quest chain, Companions/ClassQuests/ClassQuestFlow), from a
 *    giver within ANCHOR_YD of the anchor (the leader): accepted through the client's own handler
 *    (WorldSession::HandleQuestgiverAcceptQuestOpcode: every rule applies, and the giver's accept
 *    scripts run, which is right for a quest only this bot takes). Design:
 *    .kiro/docs/2026-11-18-companion-class-quests.md, stage C5 (workspace root);
 *  - the leader's (LeaderQuests: in his log, not in the bot's, the bot may take it): only at a
 *    giver the leader stands by (GiverVisit::TOGETHER_YD), taken without the giver's accept
 *    scripts (they ran for the leader: an escort, an event), as when the bot follows the
 *    leader's accept (PlayerBotMgr::OnPlayerAcceptedQuest). A quest the leader took before the
 *    bot could (the next of a chain the bot finishes later, a bot that joined later) is taken
 *    when the party is at its giver again. The leader is told (PBQUEST;<bot>;accept, the addon).
 *
 * Same gate and limits as the turn-in errand (QuestTurnIn.h), including RETRY_MS: a class
 * trainer seen from the street above (the search is 2D) fails the walk check there, and the bot
 * must still take the quest once it stands next to him. Map thread of the bot.
 */

#ifndef MANGOS_LIFE_QUEST_PICK_UP_H
#define MANGOS_LIFE_QUEST_PICK_UP_H

#include "Common.h"
#include "ObjectGuid.h"
#include "Life/Errands/ErrandCommon.h"
#include "Life/Errands/GiverVisit.h"
#include <vector>

class Player;

namespace Life
{
    struct WantedQuest
    {
        uint32 questId = 0;
        bool withLeader = false;    // the leader's quest (see above), else the bot's own
    };

    // Per bot (a member of its AI). Plain data.
    struct QuestPickUpState
    {
        ObjectGuid giver;           // creature or gameobject
        uint32 questId = 0;
        bool withLeader = false;
        uint32 startedMs = 0;
        uint32 nextSearchMs = 0;
        GiverVisitState visit;
        Errands::DoneList done;
    };

    namespace QuestPickUp
    {
        constexpr float SEARCH_YD = 30.0f;
        constexpr float ANCHOR_YD = 35.0f;
        constexpr float MAX_PATH_DETOUR = 2.5f;
        constexpr uint32 SEARCH_MS = 2000;
        constexpr uint32 GIVE_UP_MS = 20000;
        constexpr size_t DONE_KEEP = 8;
        constexpr uint32 RETRY_MS = 60000;      // a giver it gave up on is tried again after this

        // Adds the quests in `leader`'s log the bot does not have and may take now.
        void AddLeaderQuests(Player const* me, Player* leader, std::vector<WantedQuest>& wanted);

        // `wanted`: quests to take (not in the log yet). True: the bot walks to a giver or
        // stands at it this tick.
        bool Update(Player* me, QuestPickUpState& state, Player const* anchor, std::vector<WantedQuest> const& wanted);
        void Stop(Player* me, QuestPickUpState& state);
    }
}

#endif
