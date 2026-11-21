#include "Shared/Handover.h"
#include "Item.h"
#include "Mail.h"
#include "ObjectMgr.h"
#include "Player.h"
#include <algorithm>

namespace BotShared
{
namespace Handover
{
    namespace
    {
        // New items of `itemId` by mail, in stacks of at most its stack size.
        bool MailNew(Player* from, uint32 itemId, uint32 count, Player* to, std::string const& subject)
        {
            ItemPrototype const* proto = sObjectMgr.GetItemPrototype(itemId);
            if (!proto || !count)
                return false;
            uint32 const stack = std::max<uint32>(1, proto->GetMaxStackSize());
            while (count)
            {
                MailDraft draft(subject);
                for (uint32 i = 0; i < MAX_MAIL_ITEMS && count; ++i)
                {
                    uint32 const n = std::min(count, stack);
                    Item* item = Item::CreateItem(itemId, n, to->GetObjectGuid());
                    if (!item)
                        return false;
                    draft.AddItem(item);
                    count -= n;
                }
                draft.SendMailTo(MailReceiver(to), MailSender(from), MAIL_CHECK_MASK_COPIED);
            }
            return true;
        }
    }

    bool Fits(Player const* to, uint32 itemId, uint32 count)
    {
        ItemPosCountVec dest;
        return to->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, itemId, count) == EQUIP_ERR_OK;
    }

    Result MoveCount(Player* from, uint32 itemId, uint32 count, Player* to, std::string const& mailSubject)
    {
        if (!count || from->GetItemCount(itemId) < count)
            return Result::Failed;
        ItemPosCountVec dest;
        Result result = Result::Failed;
        if (to->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, itemId, count) == EQUIP_ERR_OK)
        {
            from->DestroyItemCount(itemId, count, true);
            to->StoreNewItem(dest, itemId, true);
            result = Result::Bags;
        }
        else if (!mailSubject.empty() && !to->IsSavingDisabled())
        {
            from->DestroyItemCount(itemId, count, true);
            result = MailNew(from, itemId, count, to, mailSubject) ? Result::Mailed : Result::Failed;
        }
        if (result != Result::Failed)
        {
            Save(from);
            Save(to);
        }
        return result;
    }

    Result MoveItem(Player* from, Item* item, Player* to, std::string const& mailSubject)
    {
        if (!item || item->IsInTrade())
            return Result::Failed;
        ItemPosCountVec dest;
        uint8 bagSlot = 0;
        bool const fits = to->CanStoreItem(NULL_BAG, NULL_SLOT, dest, item, bagSlot, false) == EQUIP_ERR_OK;
        if (!fits && (mailSubject.empty() || to->IsSavingDisabled()))
            return Result::Failed;

        // the steps of a trade (TradeHandler), as `.partybot giveslot`
        bool const saved = !from->IsSavingDisabled();
        from->MoveItemFromInventory(item->GetBagSlot(), item->GetSlot(), true);
        Result result;
        if (fits)
        {
            to->MoveItemToInventory(dest, item, true, saved);
            // (item may be merged into a stack and deleted now)
            result = Result::Bags;
        }
        else
        {
            if (saved)
                item->DeleteFromInventoryDB();
            item->SetOwnerGuid(to->GetObjectGuid());
            MailDraft(mailSubject).AddItem(item).SendMailTo(MailReceiver(to), MailSender(from), MAIL_CHECK_MASK_COPIED);
            result = Result::Mailed;
        }
        Save(from);
        Save(to);
        return result;
    }

    void Save(Player* player)
    {
        if (player && !player->IsSavingDisabled())
            player->SaveInventoryAndGoldToDB();
    }
}
}
