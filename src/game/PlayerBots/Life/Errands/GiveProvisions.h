/*
 * Bot daily life (PlayerBots/Life), errand: a mage or a warlock bot makes conjured supplies for
 * someone else and hands them over in a trade (user 2026-11-20). Shared/Provisions says what
 * fits whom; Shared/Trade carries the trade out.
 *
 * Who gets them:
 *  - the human leader, asked first (Requests, intent Supply):
 *    · on its own: every CHECK_MS, out of combat, when he is short (Provisions::Short: below a
 *      stack of drink with mana or food, no healthstone). A "no" ends the offers for the
 *      session (the bot's life: until the leader logs out); no answer: quiet for QUIET_MS.
 *    · when he opens a trade with the bot while short: asked about it, even after a "no"
 *      (his own trade). Not short: the trade is an ordinary one (CombatBotBaseAI).
 *    Yes: GIFT_STACKS stacks of drink (he has mana) and food, or a healthstone.
 *  - a bot of the party that asked (Life/Errands/ProvisionDesk orders, AskProvisions): the bot
 *    answers aloud (white /say) and gives what it asked for.
 *
 * The gift, one at a time:
 *   Make      the missing products are conjured, one cast after another (the best the taker
 *             can use, Provisions::RecipeFor). A window the leader opened is closed first
 *             ("稍等，我现做一些"). No power for a cast: the bot does its other business (it
 *             rests) and comes back; after MAKE_MS it gives what it has.
 *   Approach  up to the taker, within TRADE_YD, both standing.
 *   Trade     the stacks go into the window (the leader's open one, else a new one).
 *   Failed (not opened, a move apart): again after RETRY_MS, at most MAX_RETRIES times, then it
 *   says so. Declined (he closed it): over. NoRoom: it says so.
 * A gift not done after JOB_MS is given up (the taker went elsewhere).
 *
 * Map thread of the bot.
 */

#ifndef MANGOS_LIFE_GIVE_PROVISIONS_H
#define MANGOS_LIFE_GIVE_PROVISIONS_H

#include "Common.h"
#include "ObjectGuid.h"
#include "Life/Errands/ProvisionDesk.h"
#include "Life/Errands/Trip/Approach.h"
#include <string>
#include <vector>

class Player;

namespace BotShared { namespace Trade { struct Hands; } }

namespace Life
{
    enum class GivePhase : uint8
    {
        Make,
        Approach,
        Trade,
        Pause,
    };

    // Per bot (a member of its AI). Plain data.
    struct GiveProvisionsState
    {
        // The question to the leader.
        uint32 requestId = 0;
        Provision::Kinds askKinds;
        bool askedForTrade = false;     // about a trade he opened
        bool declined = false;          // he said no: no more offers (his own trades still ask)
        uint32 nextCheckMs = 0;
        uint32 quietUntilMs = 0;
        ObjectGuid tradeOpenedBy;       // a trade someone opened with the bot, not looked at yet

        // The gift.
        bool hasJob = false;
        ObjectGuid taker;
        Provision::Kinds kinds;
        uint32 orderId = 0;             // a bot's order (ProvisionDesk); 0 = the leader
        GivePhase phase = GivePhase::Make;
        uint32 phaseMs = 0;
        uint32 startMs = 0;
        uint32 failures = 0;
        bool toldMaking = false;
        bool tradeBegun = false;
        uint32 castingSpell = 0;
        Trip::ApproachState approach;
    };

    namespace GiveProvisions
    {
        constexpr uint32 CHECK_MS = 30 * IN_MILLISECONDS;
        constexpr uint32 QUIET_MS = 10 * MINUTE * IN_MILLISECONDS;
        constexpr uint32 MAKE_MS = 2 * MINUTE * IN_MILLISECONDS;
        constexpr uint32 JOB_MS = 5 * MINUTE * IN_MILLISECONDS;
        constexpr uint32 RETRY_MS = 2 * IN_MILLISECONDS;
        constexpr uint32 MAX_RETRIES = 2;
        constexpr float TRADE_YD = 3.0f;
        constexpr float ASK_RANGE_YD = 30.0f;
        // Stacks of one kind in one trade (two kinds fit the six slots).
        constexpr uint32 MAX_STACKS = 3;

        // `leader`: the party's human leader (null: none). True: busy with a gift (the caller
        // keeps it still); false while it only waits (power for the next cast, an answer).
        bool Update(Player* me, GiveProvisionsState& state, Player* leader, BotShared::Trade::Hands& hands,
                    std::vector<std::string> const& tags);

        // Someone opened a trade with the bot (SMSG_TRADE_STATUS BEGIN_TRADE).
        void OnTradeOpened(GiveProvisionsState& state, ObjectGuid from);

        // Combat, a Commander plan: its trade and its cast stop; the gift goes on afterwards.
        void Pause(Player* me, GiveProvisionsState& state, BotShared::Trade::Hands& hands);
    }
}

#endif
