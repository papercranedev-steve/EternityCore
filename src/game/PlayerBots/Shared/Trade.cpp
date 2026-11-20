#include "Shared/Trade.h"
#include "Item.h"
#include "Log.h"
#include "Map.h"
#include "Player.h"
#include "SharedDefines.h"
#include "Timer.h"
#include "TradeData.h"
#include "WorldSession.h"
#include "Packets/Trade.h"
#include <memory>

namespace BotShared
{
namespace Trade
{
    namespace
    {
        Player* PartnerOf(Player* me, Hands const& hands)
        {
            return me->FindMap() ? me->GetMap()->GetPlayer(hands.partner) : nullptr;
        }

        void Finish(Hands& hands, Result result)
        {
            hands.active = false;
            hands.result = result;
        }

        // The partner's bags take what the bot put in the slots (the server checks it only
        // once both accepted, and then only tells it in a notification).
        bool PartnerCanStore(Player* me, Player* partner)
        {
            TradeData* mine = me->GetTradeData();
            if (!mine)
                return false;
            Item* items[TRADE_SLOT_TRADED_COUNT] = {};
            for (uint8 i = 0; i < TRADE_SLOT_TRADED_COUNT; ++i)
                items[i] = mine->GetItem(TradeSlots(i));
            return partner->CanStoreItems(items, TRADE_SLOT_TRADED_COUNT) == EQUIP_ERR_OK;
        }

        // After a closing or refusing status: the window still there is a refused slot or a
        // refused accept (too far); gone, it was closed by the partner or by a move apart, or
        // it never opened.
        void SettleClosing(Player* me, Hands& hands)
        {
            hands.closing = false;
            if (me->GetTradeData())
            {
                CloseWindow(me);
                Finish(hands, Result::Failed);
                return;
            }
            if (!hands.open)
            {
                Finish(hands, Result::Failed);
                return;
            }
            Player* partner = PartnerOf(me, hands);
            bool const apart = !partner || !me->IsWithinDistInMap(partner, INTERACTION_DISTANCE - 0.5f);
            Finish(hands, apart ? Result::Failed : Result::Declined);
        }

        void PlaceItems(Player* me, Hands& hands)
        {
            uint8 slot = 0;
            for (ObjectGuid const& guid : hands.give)
            {
                Item* item = me->GetItemByGuid(guid);
                if (!item || !item->CanBeTraded() || slot >= TRADE_SLOT_TRADED_COUNT)
                    continue;
                auto packet = std::make_unique<WorldPackets::Trade::SetTradeItem>();
                packet->tradeSlot = slot++;
                packet->bag = item->GetBagSlot();
                packet->slot = item->GetSlot();
                me->GetSession()->QueuePacket(std::move(packet));
            }
            hands.placed = true;
            if (!hands.give.empty() && !slot)
            {
                CloseWindow(me);
                Finish(hands, Result::Failed);
            }
        }

        Result TakeResult(Hands& hands)
        {
            Result const result = hands.result;
            hands.result = Result::Idle;
            return result;
        }
    }

    void Begin(Player* me, Player* partner, std::vector<Item*> const& give, Hands& hands)
    {
        hands = Hands();
        hands.active = true;
        hands.partner = partner->GetObjectGuid();
        for (Item* item : give)
            if (item && hands.give.size() < MAX_ITEMS)
                hands.give.push_back(item->GetObjectGuid());
        hands.startMs = hands.lastChangeMs = WorldTimer::getMSTime();

        Player* with = OpenWith(me);
        if (with == partner)
        {
            hands.open = true;
            return;
        }
        if (with)
        {
            Finish(hands, Result::Failed);
            return;
        }
        hands.initiated = true;
        auto packet = std::make_unique<WorldPackets::Trade::InitiateTrade>();
        packet->tradeTargetGuid = partner->GetObjectGuid();
        me->GetSession()->QueuePacket(std::move(packet));
    }

    bool OnStatus(Player* me, Hands& hands, uint32 status)
    {
        if (!hands.active)
            return false;
        uint32 const now = WorldTimer::getMSTime();
        switch (status)
        {
            case TRADE_STATUS_BEGIN_TRADE:
                // (someone else asks: the server refuses it while the bot trades)
                return false;
            case TRADE_STATUS_OPEN_WINDOW:
                hands.open = true;
                hands.lastChangeMs = now;
                return true;
            case TRADE_STATUS_TRADE_ACCEPT:
                hands.partnerAccepted = true;
                return true;
            case TRADE_STATUS_BACK_TO_TRADE:
                // Something changed (either side), or the server refused the bot's accept: the
                // bot accepts again later. (The partner's accept is not cleared: a refused
                // accept of the bot does not undo it, and after a change of his he decides by
                // accepting again; the bot taking first changes nothing.)
                hands.accepted = false;
                hands.lastChangeMs = now;
                return true;
            case TRADE_STATUS_TRADE_COMPLETE:
                Finish(hands, Result::Completed);
                return true;
            default:
                hands.closing = true;
                return true;
        }
    }

    Result Update(Player* me, Hands& hands)
    {
        if (!hands.active)
            return TakeResult(hands);

        uint32 const now = WorldTimer::getMSTime();
        Player* partner = PartnerOf(me, hands);
        if (hands.closing)
            SettleClosing(me, hands);
        else if (!partner || !partner->IsInWorld())
        {
            CloseWindow(me);
            Finish(hands, Result::Failed);
        }
        else if (!hands.open)
        {
            if (WorldTimer::getMSTimeDiff(hands.startMs, now) > OPEN_TIMEOUT_MS)
            {
                CloseWindow(me);
                Finish(hands, Result::Failed);
            }
        }
        else if (!me->GetTradeData())
            Finish(hands, Result::Failed);
        else if (!hands.placed)
        {
            PlaceItems(me, hands);
            hands.lastChangeMs = now;
        }
        else if (WorldTimer::getMSTimeDiff(hands.startMs, now) > WINDOW_TIMEOUT_MS)
        {
            CloseWindow(me);
            Finish(hands, Result::Declined);
        }
        else if (!hands.accepted && WorldTimer::getMSTimeDiff(hands.lastChangeMs, now) >= ACCEPT_DELAY_MS &&
                 (!hands.give.empty() || hands.partnerAccepted))
        {
            if (!hands.give.empty() && !PartnerCanStore(me, partner))
            {
                CloseWindow(me);
                Finish(hands, Result::NoRoom);
            }
            else if (hands.accepts >= MAX_ACCEPTS)
            {
                CloseWindow(me);
                Finish(hands, Result::Failed);
            }
            else
            {
                me->GetSession()->QueuePacket(std::make_unique<WorldPackets::Trade::AcceptTrade>());
                hands.accepted = true;
                ++hands.accepts;
            }
        }

        if (!hands.active)
        {
            sLog.Out(LOG_BASIC, LOG_LVL_DETAIL, "[Trade] %s with %s: result %u.", me->GetName(),
                     partner ? partner->GetName() : "?", uint32(hands.result));
            return TakeResult(hands);
        }
        return Result::Busy;
    }

    void Cancel(Player* me, Hands& hands)
    {
        if (hands.active)
            CloseWindow(me);
        hands = Hands();
    }

    void CloseWindow(Player* me)
    {
        if (me->GetTradeData())
            me->GetSession()->QueuePacket(std::make_unique<NullClientPacket>(CMSG_CANCEL_TRADE));
    }

    Player* OpenWith(Player* me)
    {
        TradeData* trade = me->GetTradeData();
        return trade ? trade->GetTrader() : nullptr;
    }
}
}
