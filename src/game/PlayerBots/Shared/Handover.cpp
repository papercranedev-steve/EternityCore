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

        // An item taken out of a character's inventory (`saved`: of one that saves) into `to`'s,
        // the steps of a trade (TradeHandler). A temporary receiver never saves: the item must not
        // stay in the DB, or it could be duplicated.
        void Arrive(Item* item, bool saved, Player* to, ItemPosCountVec const& dest)
        {
            if (saved && to->IsSavingDisabled())
            {
                item->DeleteFromInventoryDB();
                item->DeleteAllFromDB();
            }
            to->MoveItemToInventory(dest, item, true, saved);
            // (item may be merged into a stack and deleted now)
        }

        // a slot in the bags (not worn, not the bank, not the bags themselves)
        bool IsBagSlot(uint8 bag, uint8 slot)
        {
            return slot != NULL_SLOT && Player::IsInventoryPos(bag, slot);
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

        bool const saved = !from->IsSavingDisabled();
        from->MoveItemFromInventory(item->GetBagSlot(), item->GetSlot(), true);
        Result result;
        if (fits)
        {
            Arrive(item, saved, to, dest);
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

    InventoryResult Plan(Player const* from, Item* item, Player const* to, uint8 bag, uint8 slot, Placement& placement)
    {
        placement = Placement();
        uint8 bagSlot = 0;
        if (bag == INVENTORY_SLOT_BAG_0 && slot < EQUIPMENT_SLOT_END)
        {
            uint16 eqDest = 0;
            InventoryResult const msg = to->CanEquipItem(slot, eqDest, item, to->GetItemByPos(bag, slot) != nullptr);
            if (msg != EQUIP_ERR_OK)
                return msg;
            placement.equipSlot = slot;
        }
        else if (bag != NULL_BAG)
        {
            if (to->CanStoreItem(bag, slot, placement.dest, item, bagSlot, false) == EQUIP_ERR_OK)
                return EQUIP_ERR_OK;
            placement.dest.clear();
            // (bound items fail CanStoreItem on the other side: they stay)
            Item* other = to->GetItemByPos(bag, slot);
            uint8 const fromBag = item->GetBagSlot();
            uint8 const fromSlot = item->GetSlot();
            if (other && !other->IsInTrade() && IsBagSlot(bag, slot) && IsBagSlot(fromBag, fromSlot) &&
                to->CanStoreItem(bag, slot, placement.dest, item, bagSlot, true) == EQUIP_ERR_OK &&
                from->CanStoreItem(fromBag, fromSlot, placement.backDest, other, bagSlot, true) == EQUIP_ERR_OK)
            {
                placement.back = other;
                return EQUIP_ERR_OK;
            }
            placement.dest.clear();
            placement.backDest.clear();
        }
        return to->CanStoreItem(NULL_BAG, NULL_SLOT, placement.dest, item, bagSlot, false);
    }

    Item* Place(Player* from, Item* item, Player* to, Placement const& placement)
    {
        bool const fromSaved = !from->IsSavingDisabled();
        bool const toSaved = !to->IsSavingDisabled();
        ObjectGuid const guid = item->GetObjectGuid();
        from->MoveItemFromInventory(item->GetBagSlot(), item->GetSlot(), true);
        if (placement.back)
            to->MoveItemFromInventory(placement.back->GetBagSlot(), placement.back->GetSlot(), true);
        Arrive(item, fromSaved, to, placement.dest);
        if (placement.back)
            Arrive(placement.back, toSaved, from, placement.backDest);

        // put on (checked in Plan; SwapItem tells a player's client if it still fails)
        Item* landed = to->GetItemByGuid(guid);
        if (landed && placement.equipSlot != NULL_SLOT)
        {
            to->SwapItem(landed->GetPos(), uint16(INVENTORY_SLOT_BAG_0) << 8 | placement.equipSlot);
            landed = to->GetItemByGuid(guid);
        }
        if (!landed && !placement.dest.empty())
            landed = to->GetItemByPos(placement.dest.front().pos);

        Save(from);
        Save(to);
        return landed;
    }

    void Save(Player* player)
    {
        if (player && !player->IsSavingDisabled())
            player->SaveInventoryAndGoldToDB();
    }
}
}
