#include "Shared/ItemUse.h"
#include "Player.h"
#include "Bag.h"
#include "Spell.h"
#include "SpellMgr.h"
#include "WorldSession.h"

namespace BotShared
{
    namespace ItemUse
    {
        namespace
        {
            // First stack: the backpack, then the bags.
            Item* FindInBags(Player const* player, uint32 itemId, uint8& bag, uint8& slot)
            {
                for (uint8 i = INVENTORY_SLOT_ITEM_START; i < INVENTORY_SLOT_ITEM_END; ++i)
                {
                    Item* p = player->GetItemByPos(INVENTORY_SLOT_BAG_0, i);
                    if (p && p->GetEntry() == itemId)
                    {
                        bag = INVENTORY_SLOT_BAG_0;
                        slot = i;
                        return p;
                    }
                }
                for (uint8 b = INVENTORY_SLOT_BAG_START; b < INVENTORY_SLOT_BAG_END; ++b)
                {
                    Item* pBagItem = player->GetItemByPos(INVENTORY_SLOT_BAG_0, b);
                    if (!pBagItem || !pBagItem->IsBag())
                        continue;
                    Bag* pBag = static_cast<Bag*>(pBagItem);
                    for (uint32 j = 0; j < pBag->GetBagSize(); ++j)
                    {
                        Item* p = pBag->GetItemByPos(uint8(j));
                        if (p && p->GetEntry() == itemId)
                        {
                            bag = b;
                            slot = uint8(j);
                            return p;
                        }
                    }
                }
                return nullptr;
            }

            uint8 OnUseSlot(ItemPrototype const* proto)
            {
                for (uint8 i = 0; i < MAX_ITEM_PROTO_SPELLS; ++i)
                    if (proto->Spells[i].SpellId && proto->Spells[i].SpellTrigger == ITEM_SPELLTRIGGER_ON_USE)
                        return i;
                return MAX_ITEM_PROTO_SPELLS;
            }
        }

        SpellEntry const* OnUseSpell(Player const* player, uint32 itemId)
        {
            uint8 bag = 0, slot = 0;
            Item* item = player ? FindInBags(player, itemId, bag, slot) : nullptr;
            if (!item)
                return nullptr;
            uint8 const spellSlot = OnUseSlot(item->GetProto());
            return spellSlot < MAX_ITEM_PROTO_SPELLS ? sSpellMgr.GetSpellEntry(item->GetProto()->Spells[spellSlot].SpellId) : nullptr;
        }

        bool BagItemsBlocked(Player const* player)
        {
            return player->IsShapeShifted();
        }

        bool UseFromBags(Player* player, uint32 itemId, SpellCastTargets const& targets)
        {
            if (!player || BagItemsBlocked(player))
                return false;
            uint8 bag = 0, slot = 0;
            Item* pItem = player ? FindInBags(player, itemId, bag, slot) : nullptr;
            if (!pItem)
                return false;

            ItemPrototype const* pProto = pItem->GetProto();
            uint8 const spellSlot = OnUseSlot(pProto);
            SpellEntry const* pSpellEntry = spellSlot < MAX_ITEM_PROTO_SPELLS ? sSpellMgr.GetSpellEntry(pProto->Spells[spellSlot].SpellId) : nullptr;
            if (!pSpellEntry || player->CanUseItem(pItem) != EQUIP_ERR_OK || !player->IsSpellReady(pSpellEntry, pProto))
                return false;

            uint32 const countBefore = player->GetItemCount(itemId);
            WorldPackets::Spell::UseItem packet;
            packet.bagIndex = bag;
            packet.slot = slot;
            packet.spellSlot = spellSlot;
            packet.targets = targets;
            player->GetSession()->HandleUseItemOpcode(packet);
            // pItem may be deleted now (last charge used).

            // (an item spell with a cast time, a poison vial: consumed when the cast ends; a
            // channel, a taming rod: going on)
            auto casting = [&](CurrentSpellTypes type)
            {
                Spell const* pCasting = player->GetCurrentSpell(type);
                return pCasting && pCasting->m_spellInfo && pCasting->m_spellInfo->Id == pSpellEntry->Id;
            };
            Spell const* pGeneric = player->GetCurrentSpell(CURRENT_GENERIC_SPELL);
            bool const preparing = casting(CURRENT_GENERIC_SPELL) && pGeneric->getState() == SPELL_STATE_PREPARING;
            return preparing || casting(CURRENT_CHANNELED_SPELL) || player->GetItemCount(itemId) < countBefore ||
                   !player->IsSpellReady(pSpellEntry, pProto);
        }
    }
}
