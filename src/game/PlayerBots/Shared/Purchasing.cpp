#include "Shared/Purchasing.h"
#include "Shared/Inventory.h"
#include "Player.h"
#include "ItemPrototype.h"
#include <algorithm>

namespace BotShared
{
namespace Purchasing
{
    Budget Budget::Of(Player const* player, uint32 reserve)
    {
        Budget b;
        b.money = player->GetMoney();
        b.freeSlots = Inventory::FreeBagSlots(player);
        b.reserve = reserve;
        return b;
    }

    uint32 Budget::MoneyFor(Supplies::Priority priority) const
    {
        if (priority == Supplies::Priority::Critical)
            return money;
        return money > reserve ? money - reserve : 0;
    }

    uint32 Budget::SlotsFor(Supplies::Priority priority) const
    {
        if (priority == Supplies::Priority::Critical)
            return freeSlots;
        return freeSlots > KEEP_FREE_SLOTS ? freeSlots - KEEP_FREE_SLOTS : 0;
    }

    uint32 Budget::StillMissing(Supplies::Need const& need) const
    {
        auto itr = planned.find(need.goods);
        uint32 const already = itr != planned.end() ? itr->second : 0;
        return need.Missing() > already ? need.Missing() - already : 0;
    }

    void Budget::Spend(Goods::Spec const& goods, uint32 itemId, uint32 units, uint32 cost, uint32 slots)
    {
        money = money > cost ? money - cost : 0;
        freeSlots = freeSlots > slots ? freeSlots - slots : 0;
        planned[goods] += units;
        plannedOfItem[itemId] += units;
    }

    uint32 SlotsFor(Player const* player, ItemPrototype const* proto, uint32 units, uint32 plannedUnits)
    {
        uint32 const stack = std::max<uint32>(proto->GetMaxStackSize(), 1);
        uint32 const have = player->GetItemCount(proto->ItemId) + plannedUnits;
        uint32 const room = have % stack ? stack - have % stack : 0;
        uint32 const extra = units > room ? units - room : 0;
        return (extra + stack - 1) / stack;
    }
}
}
