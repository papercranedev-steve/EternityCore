#include "Shared/BagSorting.h"
#include "Shared/GearUpgrade.h"
#include "Shared/GivenItems.h"
#include "Shared/Inventory.h"
#include "Shared/MarketValue.h"
#include "Shared/Supplies.h"
#include "Shared/Goods.h"
#include "AuctionHouseBuyerBot.h"
#include "Bag.h"
#include "Item.h"
#include "ObjectMgr.h"
#include "ItemPrototype.h"
#include "Player.h"
#include "SpellMgr.h"
#include "Log.h"
#include <algorithm>

namespace BotShared
{
namespace BagSorting
{
    namespace
    {
        using Kept = std::vector<Goods::Spec>;

        // A health or mana potion / gem the player can use (Commander/Classes/ItemKit drinks them).
        bool RestoresHealthOrMana(Player const* player, ItemPrototype const* proto)
        {
            if (proto->Class != ITEM_CLASS_CONSUMABLE || player->CanUseItem(proto) != EQUIP_ERR_OK)
                return false;
            for (auto const& s : proto->Spells)
            {
                if (!s.SpellId || s.SpellTrigger != ITEM_SPELLTRIGGER_ON_USE)
                    continue;
                SpellEntry const* use = sSpellMgr.GetSpellEntry(s.SpellId);
                if (!use)
                    continue;
                for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
                {
                    if (use->Effect[i] == SPELL_EFFECT_HEAL)
                        return true;
                    if (use->Effect[i] == SPELL_EFFECT_ENERGIZE && use->EffectMiscValue[i] == POWER_MANA &&
                        player->GetMaxPower(POWER_MANA) > 0)
                        return true;
                }
            }
            return false;
        }

        bool IsGear(ItemPrototype const* proto)
        {
            return proto->Class == ITEM_CLASS_WEAPON || proto->Class == ITEM_CLASS_ARMOR ||
                   proto->Class == ITEM_CLASS_CONTAINER || proto->Class == ITEM_CLASS_QUIVER;
        }

        // The auction house pays more for the stack than a vendor, after its cut.
        bool SellsAtMarket(Item const* item, ItemPrototype const* proto)
        {
            if (!sAuctionHouseBuyerBot.IsEnabled() || !item->CanBeTraded())
                return false;
            uint32 const count = std::max<uint32>(item->GetCount(), 1);
            uint32 const price = MarketValue::AuctionPrice(proto->ItemId, count);
            return price && uint64(price) * (100 - MARKET_CUT_PCT) > uint64(proto->SellPrice) * count * 100;
        }

        Fate FateOf(Player const* player, Item const* item, Rules const& rules, Kept const& kept)
        {
            ItemPrototype const* proto = item->GetProto();
            if (!proto || item->IsInTrade())
                return Fate::Keep;
            if (proto->Class == ITEM_CLASS_QUEST || proto->StartQuest || proto->Class == ITEM_CLASS_KEY ||
                player->HasQuestForItem(proto->ItemId))
                return Fate::Keep;
            if ((proto->Flags & ITEM_FLAG_CONJURED) || item->GetUInt32Value(ITEM_FIELD_DURATION))
                return Fate::Keep;
            for (Goods::Spec const& spec : kept)
                if (Goods::Matches(player, spec, proto))
                    return Fate::Keep;
            if (RestoresHealthOrMana(player, proto))
                return Fate::Keep;

            if (proto->Quality == ITEM_QUALITY_POOR)
                return proto->SellPrice ? Fate::Vendor : Fate::Trash;

            if (GivenItems::IsGiven(item))
                return Fate::Keep;
            if (proto->Class == ITEM_CLASS_CONTAINER ? GearUpgrade::IsBagUpgrade(player, proto)
                                                     : GearUpgrade::IsUpgrade(player, item))
                return Fate::Keep;
            if (rules.keepUnboundGear && IsGear(proto) && !item->IsSoulBound())
                return Fate::Keep;

            bool const market = SellsAtMarket(item, proto);
            if (!market && !proto->SellPrice)
                return Fate::Keep;
            return market ? Fate::Market : Fate::Vendor;
        }

        uint32 WorthOf(Item const* item, Fate fate)
        {
            ItemPrototype const* proto = item->GetProto();
            uint32 const count = std::max<uint32>(item->GetCount(), 1);
            if (fate == Fate::Market)
                return MarketValue::AuctionPrice(proto->ItemId, count);
            return proto->SellPrice * count;
        }

        struct Candidate
        {
            Item* item;
            uint32 worth;
        };

        // What may be destroyed for room worth `worth`, the cheapest first.
        std::vector<Candidate> Candidates(Player const* player, Rules const& rules, uint32 worth)
        {
            Kept const kept = Supplies::KeptGoods(player);
            std::vector<Candidate> out;
            for (Item* item : Inventory::BagItems(player))
            {
                if (item->IsBag() && !((Bag*)item)->IsEmpty())
                    continue;
                Fate const fate = FateOf(player, item, rules, kept);
                if (fate == Fate::Keep)
                    continue;
                uint32 const w = WorthOf(item, fate);
                if (w < worth)
                    out.push_back({ item, w });
            }
            std::stable_sort(out.begin(), out.end(), [](Candidate const& a, Candidate const& b) { return a.worth < b.worth; });
            return out;
        }

        // Free slots `itemId` x `count` still needs (0 = it fits).
        uint32 SlotsShort(Player const* player, uint32 itemId, uint32 count)
        {
            ItemPosCountVec dest;
            uint32 noSpace = 0;
            if (player->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, itemId, count, &noSpace) == EQUIP_ERR_OK)
                return 0;
            ItemPrototype const* proto = sObjectMgr.GetItemPrototype(itemId);
            uint32 const stack = proto ? std::max<uint32>(proto->GetMaxStackSize(), 1) : 1;
            uint32 const missing = noSpace ? noSpace : count;
            return (missing + stack - 1) / stack;
        }

        void Destroy(Player* player, Item* item, uint32 worth)
        {
            sLog.Out(LOG_BASIC, LOG_LVL_DETAIL, "[Bags] %s destroys %u x item %u (worth %u copper) for room.",
                player->GetName(), item->GetCount(), item->GetEntry(), worth);
            player->DestroyItem(item->GetBagSlot(), item->GetSlot(), true);
        }
    }

    Fate FateOf(Player const* player, Item const* item, Rules const& rules)
    {
        return FateOf(player, item, rules, Supplies::KeptGoods(player));
    }

    uint32 WorthOf(Player const* player, Item const* item, Rules const& rules)
    {
        return WorthOf(item, FateOf(player, item, rules));
    }

    uint32 WorthOf(uint32 itemId, uint32 count)
    {
        return MarketValue::WorthOf(itemId) * std::max<uint32>(count, 1);
    }

    std::vector<Item*> WithFate(Player const* player, Rules const& rules, std::vector<Fate> const& fates)
    {
        Kept const kept = Supplies::KeptGoods(player);
        std::vector<Item*> out;
        for (Item* item : Inventory::BagItems(player))
        {
            if (item->IsBag() && !((Bag*)item)->IsEmpty())
                continue;
            Fate const fate = FateOf(player, item, rules, kept);
            if (std::find(fates.begin(), fates.end(), fate) != fates.end())
                out.push_back(item);
        }
        return out;
    }

    bool HasAny(Player const* player, Rules const& rules, std::vector<Fate> const& fates)
    {
        return !WithFate(player, rules, fates).empty();
    }

    bool MakeRoom(Player* player, Rules const& rules, uint32 slots, uint32 worth)
    {
        uint32 free = Inventory::FreeBagSlots(player);
        if (free >= slots)
            return true;
        for (Candidate const& c : Candidates(player, rules, worth))
        {
            Destroy(player, c.item, c.worth);
            if (++free >= slots)
                return true;
        }
        return false;
    }

    bool MakeRoomFor(Player* player, Rules const& rules, uint32 itemId, uint32 count, uint32 worth)
    {
        uint32 const short_ = SlotsShort(player, itemId, count);
        if (!short_)
            return true;
        return MakeRoom(player, rules, Inventory::FreeBagSlots(player) + short_, worth);
    }

    bool CanMakeRoomFor(Player const* player, Rules const& rules, uint32 itemId, uint32 count, uint32 worth)
    {
        uint32 const short_ = SlotsShort(player, itemId, count);
        return !short_ || Candidates(player, rules, worth).size() >= short_;
    }
}
}
