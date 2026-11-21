#include "Companions/Affinity/TradeGate.h"
#include "Companions/Affinity/Gates.h"
#include "Shared/FoodAndDrink.h"
#include "Shared/GearUpgrade.h"
#include "Player.h"
#include "TradeData.h"
#include "Item.h"
#include "ItemPrototype.h"

namespace Companions
{
    namespace TradeGate
    {
        namespace
        {
            // Food, drink, or gear it would wear over what it has.
            bool Basic(Player const* me, ItemPrototype const* proto)
            {
                using BotShared::FoodAndDrink::Kind;
                if (BotShared::FoodAndDrink::ItemSpell(proto, Kind::Food) || BotShared::FoodAndDrink::ItemSpell(proto, Kind::Drink))
                    return true;
                return proto->InventoryType != INVTYPE_NON_EQUIP && BotShared::GearUpgrade::IsUpgrade(me, proto);
            }
        }

        Refusal Check(Player* me)
        {
            TradeData* mine = me->GetTradeData();
            TradeData* his = mine ? mine->GetTraderData() : nullptr;
            if (!his)
                return Refusal::None;

            bool offered = his->GetMoney() > 0;
            bool onlyBasic = true;
            for (uint32 slot = 0; slot < TRADE_SLOT_COUNT; ++slot)
                if (Item* item = his->GetItem(TradeSlots(slot)))
                {
                    offered = true;
                    if (slot == TRADE_SLOT_NONTRADED || !Basic(me, item->GetProto()))
                        onlyBasic = false;
                }
            if (!offered)
                return Refusal::None;

            uint32 const guid = me->GetGUIDLow();
            if (Gates::Allows(guid, Gates::Gate::TradeAnything))
                return Refusal::None;
            if (!Gates::Allows(guid, Gates::Gate::TradeBasics))
                return Refusal::TooEarly;
            return onlyBasic ? Refusal::None : Refusal::NotAllowed;
        }

        char const* PhraseOf(Refusal refusal)
        {
            return refusal == Refusal::TooEarly ? "affinity.trade.early" : "affinity.trade.notallowed";
        }
    }
}
