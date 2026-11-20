/*
 * Bot daily life (PlayerBots/Life), errand: turn in its finished quests where the party stands,
 * as a player of the group does: a quest giver near the anchor (the leader) that ends one of
 * its complete quests (Creature / GameObject HasInvolvedQuest, Player::CanRewardQuest) is
 * walked to and the quest is rewarded through the client's own handler
 * (WorldSession::HandleQuestgiverChooseRewardOpcode: every reward rule applies, bags full
 * means no reward). The reward comes from an IRewardChooser (RewardChooser.h); upgrades
 * among the rewards are put on. Soul companions doc §4.3 (stage S3).
 *
 * Same gate as the other errands (ErrandCommon::MayRun), safe walk, SEARCH_YD of the bot and
 * ANCHOR_YD of the anchor. A giver it could not reach in GIVE_UP_MS or that refused the
 * reward is not tried again for RETRY_MS (a giver stays where it is: from another spot, a
 * floor lower, the walk may work).
 *
 * Map thread of the bot.
 */

#ifndef MANGOS_LIFE_QUEST_TURN_IN_H
#define MANGOS_LIFE_QUEST_TURN_IN_H

#include "Common.h"
#include "ObjectGuid.h"
#include "Life/Errands/ErrandCommon.h"

class Player;

namespace Life
{
    class IRewardChooser;

    // Per bot (a member of its AI). Plain data.
    struct QuestTurnInState
    {
        ObjectGuid giver;           // creature or gameobject
        uint32 questId = 0;
        uint32 startedMs = 0;
        uint32 nextMoveMs = 0;
        uint32 nextSearchMs = 0;
        uint32 choiceSinceMs = 0;   // standing at the giver waiting for the chooser, 0 = not yet
        Errands::DoneList done;
    };

    namespace QuestTurnIn
    {
        constexpr float SEARCH_YD = 30.0f;
        constexpr float ANCHOR_YD = 35.0f;
        constexpr float MAX_PATH_DETOUR = 2.5f;
        constexpr uint32 SEARCH_MS = 2000;
        constexpr uint32 REPATH_MS = 2000;
        constexpr uint32 GIVE_UP_MS = 20000;
        // A cap only: a chooser asking the player gets the request board's answer first (its
        // timeout runs once the leader's earlier questions are done; several companions
        // turning in together are asked one after another).
        constexpr uint32 CHOICE_WAIT_MS = 180000;
        constexpr size_t DONE_KEEP = 8;
        constexpr uint32 RETRY_MS = 60000;      // a giver it gave up on is tried again after this

        // `anchor`: givers near it only (the party leader); null = near the bot.
        // True: the bot walks to a giver or stands at it this tick.
        bool Update(Player* me, QuestTurnInState& state, Player const* anchor, IRewardChooser& chooser);
        void Stop(Player* me, QuestTurnInState& state, IRewardChooser& chooser);
    }
}

#endif
