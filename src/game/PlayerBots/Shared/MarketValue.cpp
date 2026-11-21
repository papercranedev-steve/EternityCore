#include "Shared/MarketValue.h"
#include "AuctionHouseBotMgr.h"
#include "ObjectMgr.h"
#include "ItemPrototype.h"

namespace BotShared
{
    namespace MarketValue
    {
        uint32 AuctionPrice(uint32 itemId)
        {
            double price = 0.0;
            if (!sAuctionHouseBotMgr.GetReferenceUnitPrice(itemId, price) || price <= 0.0)
                return 0;
            return std::max<uint32>(1, uint32(price + 0.5));
        }

        uint32 WorthOf(uint32 itemId)
        {
            if (uint32 const auction = AuctionPrice(itemId))
                return auction;
            ItemPrototype const* proto = sObjectMgr.GetItemPrototype(itemId);
            return proto ? proto->SellPrice : 0;
        }
    }
}
