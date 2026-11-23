/*
 * Bot daily life (PlayerBots/Life), errand: turn in its finished quests where the party stands,
 * as a player of the group does: a quest giver near the anchor (the leader) that ends one of
 * its complete quests (Creature / GameObject HasInvolvedQuest, Player::CanRewardQuest) is
 * walked to and the quest is rewarded through the client's own handler
 * (WorldSession::HandleQuestgiverChooseRewardOpcode: every reward rule applies, bags full
 * means no reward). The reward comes from an IRewardChooser (RewardChooser.h); upgrades
 * among the rewards are put on. Soul companions doc §4.3 (stage S3).
 *
 * At the giver as a player (GiverVisit.h): its own spot in front of it, a moment talking before
 * the reward, a moment after. A quest the leader has (or had) too is handed in with him: only
 * at a giver he stands by (GiverVisit::TOGETHER_YD); the bot's own (a class quest) at any giver
 * within ANCHOR_YD of the anchor. What it handed in is kept for the caller (TakeTurnedIn: the
 * party voice's line, PartyBotAI).
 *
 * Same gate as the other errands (ErrandCommon::MayRun), safe walk, SEARCH_YD of the bot. A
 * giver it could not reach in GIVE_UP_MS or that refused the reward is not tried again for
 * RETRY_MS (a giver stays where it is: from another spot, a floor lower, the walk may work).
 *
 * Map thread of the bot.
 */

#ifndef MANGOS_LIFE_QUEST_TURN_IN_H
#define MANGOS_LIFE_QUEST_TURN_IN_H

#include "Common.h"
#include "ObjectGuid.h"
#include "Life/Errands/ErrandCommon.h"
#include "Life/Errands/GiverVisit.h"
#include <vector>

class Player;

namespace BotShared
{
    namespace BagSorting
    {
        struct Rules;
    }
}

namespace Life
{
    class IRewardChooser;

    // A quest the bot handed in.
    struct TurnedInQuest
    {
        uint32 questId = 0;
        ObjectGuid giver;
        uint32 itemId = 0;          // the reward it chose, else the quest's first reward item, 0 = none
        bool wore = false;          // it put on an item it got
    };

    // A quest the giver would not reward: no room for its items, even after making room.
    struct RefusedQuest
    {
        uint32 questId = 0;
        ObjectGuid giver;
    };

    // Per bot (a member of its AI). Plain data.
    struct QuestTurnInState
    {
        ObjectGuid giver;           // creature or gameobject
        uint32 questId = 0;
        uint32 startedMs = 0;
        uint32 nextSearchMs = 0;
        uint32 choiceSinceMs = 0;   // standing at the giver waiting for the chooser, 0 = not yet
        GiverVisitState visit;
        std::vector<TurnedInQuest> turnedIn;    // not taken by the caller yet
        std::vector<RefusedQuest> refused;      // not taken by the caller yet
        Errands::DoneList done;
    };

    namespace QuestTurnIn
    {
        constexpr float SEARCH_YD = 30.0f;
        constexpr float ANCHOR_YD = 35.0f;
        constexpr float MAX_PATH_DETOUR = 2.5f;
        constexpr uint32 SEARCH_MS = 2000;
        constexpr uint32 GIVE_UP_MS = 20000;
        constexpr size_t TURNED_IN_KEEP = 8;
        // A cap only: a chooser asking the player gets the request board's answer first (its
        // timeout runs once the leader's earlier questions are done; several companions
        // turning in together are asked one after another).
        constexpr uint32 CHOICE_WAIT_MS = 180000;
        constexpr size_t DONE_KEEP = 8;
        constexpr uint32 RETRY_MS = 60000;      // a giver it gave up on is tried again after this

        // `anchor`: givers near it only (the party leader); null = near the bot.
        // `bags`: the reward items get room first (Shared/BagSorting::MakeRoom, user 2026-11-23:
        // anything not kept may go); null = bags full means no reward.
        // True: the bot walks to a giver or stands at it this tick.
        bool Update(Player* me, QuestTurnInState& state, Player const* anchor, IRewardChooser& chooser,
                    BotShared::BagSorting::Rules const* bags = nullptr);
        void Stop(Player* me, QuestTurnInState& state, IRewardChooser& chooser);

        // The quests handed in since the last call, oldest first.
        std::vector<TurnedInQuest> TakeTurnedIn(QuestTurnInState& state);
        // The quests refused since the last call (bags full; tried again after RETRY_MS).
        std::vector<RefusedQuest> TakeRefused(QuestTurnInState& state);
    }
}

#endif
