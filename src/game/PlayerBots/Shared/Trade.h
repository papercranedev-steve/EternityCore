/*
 * Shared bot hands (PlayerBots/Shared): one trade with a player or another bot, through the
 * bot's own session as a client does it (Handlers/TradeHandler.cpp). The caller decides who
 * and what (Life/Errands/GiveProvisions, AskProvisions); the hands only carry it out and say
 * how it ended.
 *
 *   Begin    a trade already open with the partner (he opened it) is used as it is; else the
 *            bot asks for one (CMSG_INITIATE_TRADE). The caller stands the bot next to the
 *            partner first: a move past INTERACTION_DISTANCE closes the window.
 *   Open     the items to give go into the slots, one stack per slot.
 *   Accept   giving: the bot accepts ACCEPT_DELAY_MS after the last change of either side (the
 *            server refuses an accept in the second of a change: back to trade, accepted again
 *            later). Receiving (nothing to give): only after the partner accepted.
 *   Result   Completed; Declined (the partner closed the window, or left it open past
 *            WINDOW_TIMEOUT_MS); NoRoom (the partner's bags cannot take the items: the bot
 *            closes the window); Failed (it never opened: busy, too far, dead ...; a move
 *            apart closed it; the partner is gone).
 *
 * While active the hands own the bot's SMSG_TRADE_STATUS: the default handling (CombatBotBaseAI:
 * accept anything, equip or use what came in) does not see it.
 *
 * Map thread of the bot.
 */

#ifndef MANGOS_BOTSHARED_TRADE_H
#define MANGOS_BOTSHARED_TRADE_H

#include "Common.h"
#include "ObjectGuid.h"
#include <vector>

class Item;
class Player;

namespace BotShared
{
    namespace Trade
    {
        enum class Result : uint8
        {
            Idle,       // no trade
            Busy,       // going on
            Completed,
            Declined,
            NoRoom,
            Failed,
        };

        // Per bot (a member of its AI). Plain data.
        struct Hands
        {
            bool active = false;
            ObjectGuid partner;
            std::vector<ObjectGuid> give;   // bag items, in slot order
            bool initiated = false;         // the bot asked for the trade
            bool open = false;              // the window is open on both sides
            bool placed = false;            // the items are in the slots
            bool accepted = false;          // the bot's accept stands
            bool partnerAccepted = false;
            // A closing or refusing status came; the next Update sees whether the window is
            // still there (the server sends the status before it drops the trade).
            bool closing = false;
            uint32 startMs = 0;
            uint32 lastChangeMs = 0;
            uint32 accepts = 0;
            Result result = Result::Idle;   // a finished trade's, until Update returns it
        };

        constexpr uint32 OPEN_TIMEOUT_MS = 10 * IN_MILLISECONDS;
        constexpr uint32 WINDOW_TIMEOUT_MS = 60 * IN_MILLISECONDS;
        constexpr uint32 ACCEPT_DELAY_MS = 1200;
        constexpr uint32 MAX_ACCEPTS = 10;
        // At most this many stacks change hands (the seventh slot is not traded).
        constexpr uint32 MAX_ITEMS = 6;

        // A new trade with `partner`, giving `give` (empty: the bot only takes).
        void Begin(Player* me, Player* partner, std::vector<Item*> const& give, Hands& hands);

        // The bot's SMSG_TRADE_STATUS. True: the hands own it (do not pass it on).
        bool OnStatus(Player* me, Hands& hands, uint32 status);

        // Every bot update. Busy while it goes on; a finished trade's result once, then Idle.
        Result Update(Player* me, Hands& hands);

        // Ends it now (the caller no longer wants it): closes the window, no result.
        void Cancel(Player* me, Hands& hands);

        // Closes any trade window the bot has open (the hands' or one the partner opened).
        void CloseWindow(Player* me);

        // The player the bot has a trade window open with (any trade, the hands' or not).
        Player* OpenWith(Player* me);
    }
}

#endif
