#include "Shared/ObjectUse.h"
#include "Player.h"
#include "GameObject.h"
#include "Bag.h"
#include "Item.h"
#include "SpellMgr.h"
#include "SpellEntry.h"
#include "DBCStores.h"
#include "WorldSession.h"

namespace BotShared
{
namespace ObjectUse
{
    namespace
    {
        // Both lock effects, as Spell::CanOpenLock: a quest item that is the key of a quest object
        // (Bashal'Aran's Ancient Flame, Sharpbeak's Cage) opens it with SPELL_EFFECT_OPEN_LOCK_ITEM.
        bool OpensLock(SpellEntry const* spell, uint32 lockType, int32* skillBonus = nullptr)
        {
            for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
            {
                if (spell->Effect[i] != SPELL_EFFECT_OPEN_LOCK && spell->Effect[i] != SPELL_EFFECT_OPEN_LOCK_ITEM)
                    continue;
                if (lockType && uint32(spell->EffectMiscValue[i]) != lockType)
                    continue;
                if (skillBonus)
                    *skillBonus = spell->EffectBasePoints[i] + 1;
                return true;
            }
            return false;
        }

        Item* FindInBags(Player* player, uint32 entry)
        {
            for (uint8 i = INVENTORY_SLOT_ITEM_START; i < INVENTORY_SLOT_ITEM_END; ++i)
                if (Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
                    if (item->GetEntry() == entry)
                        return item;
            for (uint8 i = INVENTORY_SLOT_BAG_START; i < INVENTORY_SLOT_BAG_END; ++i)
                if (Bag* bag = (Bag*)player->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
                    for (uint32 j = 0; j < bag->GetBagSize(); ++j)
                        if (Item* item = bag->GetItemByPos(uint8(j)))
                            if (item->GetEntry() == entry)
                                return item;
            return nullptr;
        }

        // The key's own spell that opens locks (Spell::CanOpenLock wants it cast from the key).
        SpellEntry const* KeySpell(Item const* key)
        {
            ItemPrototype const* proto = key->GetProto();
            for (auto const& s : proto->Spells)
                if (SpellEntry const* spell = sSpellMgr.GetSpellEntry(s.SpellId))
                    if (OpensLock(spell, 0))
                        return spell;
            return nullptr;
        }

        // A known spell for the lock type that is strong enough (skill + the spell's own bonus).
        SpellEntry const* KnownSpellFor(Player* player, uint32 lockType, uint32 reqSkill)
        {
            SkillType const skill = SkillByLockType(LockType(lockType));
            for (auto const& entry : player->GetSpellMap())
            {
                if (entry.second.state == PLAYERSPELL_REMOVED || entry.second.disabled)
                    continue;
                SpellEntry const* spell = sSpellMgr.GetSpellEntry(entry.first);
                int32 bonus = 0;
                if (!spell || !OpensLock(spell, lockType, &bonus))
                    continue;
                if (skill != SKILL_NONE && int32(player->GetSkillValue(skill)) + bonus < int32(reqSkill))
                    continue;
                return spell;
            }
            return nullptr;
        }
    }

    Plan HowToUse(Player* player, GameObject* go)
    {
        Plan plan;
        if (!go || !go->isSpawned() || go->HasFlag(GAMEOBJECT_FLAGS, GO_FLAG_NO_INTERACT | GO_FLAG_IN_USE))
            return plan;

        GameObjectInfo const* info = go->GetGOInfo();
        uint32 const lockId = info->GetLockId();
        if (!lockId || info->type == GAMEOBJECT_TYPE_DOOR)
        {
            if (info->type == GAMEOBJECT_TYPE_CHEST)
                plan.way = Way::Loot;
            else if (go->PlayerCanUse(player))
                plan.way = Way::Use;
            return plan;
        }

        LockEntry const* lock = sLockStore.LookupEntry(lockId);
        if (!lock)
            return plan;
        for (int j = 0; j < MAX_LOCK_CASE; ++j)
        {
            if (lock->Type[j] == LOCK_KEY_ITEM && lock->Index[j])
            {
                if (Item* key = FindInBags(player, lock->Index[j]))
                {
                    if (SpellEntry const* spell = KeySpell(key))
                    {
                        plan.way = Way::Spell;
                        plan.spell = spell;
                        plan.item = key->GetObjectGuid();
                        return plan;
                    }
                }
            }
            else if (lock->Type[j] == LOCK_KEY_SKILL)
            {
                if (SpellEntry const* spell = KnownSpellFor(player, lock->Index[j], lock->Skill[j]))
                {
                    plan.way = Way::Spell;
                    plan.spell = spell;
                    return plan;
                }
            }
        }
        return plan;
    }

    bool Start(Player* player, GameObject* go, Plan const& plan)
    {
        if (!go || !go->IsAtInteractDistance(player))
            return false;

        switch (plan.way)
        {
            case Way::Use:
            {
                WorldPackets::Misc::GameObjectUse packet;
                packet.guid = go->GetObjectGuid();
                player->GetSession()->HandleGameObjectUseOpcode(packet);
                return true;
            }
            case Way::Loot:
                player->SendLoot(go->GetObjectGuid(), LOOT_CORPSE);
                return player->GetLootGuid() == go->GetObjectGuid();
            case Way::Spell:
            {
                Item* key = plan.item.IsEmpty() ? nullptr : player->GetItemByGuid(plan.item);
                if (!plan.item.IsEmpty() && !key)
                    return false;
                return player->CastSpell(go, plan.spell, false, key) == SPELL_CAST_OK;
            }
            default:
                return false;
        }
    }
}
}
