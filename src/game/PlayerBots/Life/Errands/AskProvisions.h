/*
 * Bot daily life (PlayerBots/Life), errand: a bot short of food or drink asks a mage of its
 * party for conjured ones instead of buying them (user 2026-11-20).
 *
 * The giver: a mage bot of the party whose conjured goods are not far worse than what this bot
 * can buy (Provisions::GoodEnough), else the human leader when he is a mage. While one is there,
 * Supplies::SetProvided keeps the shopping from buying food and drink; without one (or after
 * it failed, was refused or ignored) the bot buys again.
 *
 * Short (Provisions::Short): below a stack of food, or of drink with mana. Then, out of combat:
 *  - a mage bot: the bot walks up to it, asks aloud (white /say) and posts the order
 *    (ProvisionDesk); it waits by the mage until the mage traded (GiveProvisions) or failed.
 *    Failed or not done in ORDER_MS: that mage is not asked again for GIVER_QUIET_MS.
 *  - the human mage leader: the bots queue up for him and one question asks for all of them
 *    (ProvisionDesk::Want). Yes: when its turn comes the bot walks up and opens a trade
 *    window, he puts water and food in, the bot accepts after him. He gives nothing or
 *    closes the window: refused, the bots do not ask him again this session. The trade
 *    itself fails (not opened, a move apart): again after RETRY_MS, at most MAX_RETRIES times.
 * After a gift the bot does not ask again for AFTER_GIFT_MS (a small gift is not asked again
 * and again).
 *
 * Map thread of the bot.
 */

#ifndef MANGOS_LIFE_ASK_PROVISIONS_H
#define MANGOS_LIFE_ASK_PROVISIONS_H

#include "Common.h"
#include "ObjectGuid.h"
#include "Life/Errands/ProvisionDesk.h"
#include "Life/Errands/Trip/Approach.h"
#include <map>
#include <string>
#include <vector>

class Player;

namespace BotShared { namespace Trade { struct Hands; } }

namespace Life
{
    // Per bot (a member of its AI). Plain data.
    struct AskProvisionsState
    {
        uint32 nextAskMs = 0;                       // after a gift
        uint32 nextCheckMs = 0;                     // idle: looks again then
        std::map<ObjectGuid, uint32> giverQuietUntilMs;
        // A mage bot's order.
        uint32 orderId = 0;
        ObjectGuid giver;
        uint32 orderMs = 0;
        Provision::Kinds orderKinds;
        bool asked = false;
        // The human mage leader.
        bool leaderRefused = false;                 // for the bot's session
        uint32 leaderQuietUntilMs = 0;
        bool queued = false;
        bool trading = false;
        uint32 failures = 0;
        uint32 pauseUntilMs = 0;
        uint32 heldBefore[2] = {};                  // drink, food when the trade began
        Trip::ApproachState approach;
    };

    namespace AskProvisions
    {
        // Idle (nothing asked): who gives and what is short is looked at this often.
        constexpr uint32 CHECK_MS = 2 * IN_MILLISECONDS;
        constexpr uint32 ORDER_MS = 3 * MINUTE * IN_MILLISECONDS;
        constexpr uint32 GIVER_QUIET_MS = 30 * MINUTE * IN_MILLISECONDS;
        constexpr uint32 AFTER_GIFT_MS = 10 * MINUTE * IN_MILLISECONDS;
        constexpr uint32 RETRY_MS = 2 * IN_MILLISECONDS;
        constexpr uint32 MAX_RETRIES = 2;
        constexpr float NEAR_YD = 3.0f;

        // `leader`: the party's leader (null: none); `mates`: the other party bots on its map
        // (the mages among them may give). True: busy (walking up, waiting by the giver,
        // trading: the caller keeps it still).
        bool Update(Player* me, AskProvisionsState& state, Player* leader, std::vector<Player*> const& mates,
                    BotShared::Trade::Hands& hands, std::vector<std::string> const& tags);

        // Combat, a Commander plan: a trade going on closes (counted as a failed try), the
        // order and the queue place stay.
        void Pause(Player* me, AskProvisionsState& state, BotShared::Trade::Hands& hands);
    }
}

#endif
