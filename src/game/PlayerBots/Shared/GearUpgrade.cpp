#include "Shared/GearUpgrade.h"
#include "Shared/GearFit.h"
#include "Shared/GearSources.h"
#include "Shared/Inventory.h"
#include "Player.h"
#include "Item.h"
#include "ItemPrototype.h"
#include "SharedDefines.h"
#include <algorithm>
#include <mutex>
#include <unordered_map>

namespace BotShared
{
namespace GearUpgrade
{
    namespace
    {
        // Score per item level between two items the score cannot see (trinkets, relics).
        constexpr float ITEM_LEVEL_POINT = 0.01f;

        std::mutex s_rolesLock;
        std::unordered_map<uint32, uint8> s_roles;     // player guid (low) -> role

        bool IsBodyArmor(uint32 inventoryType)
        {
            switch (inventoryType)
            {
                case INVTYPE_HEAD:
                case INVTYPE_SHOULDERS:
                case INVTYPE_CHEST:
                case INVTYPE_ROBE:
                case INVTYPE_WAIST:
                case INVTYPE_LEGS:
                case INVTYPE_FEET:
                case INVTYPE_WRISTS:
                case INVTYPE_HANDS:
                    return true;
                default:
                    return false;   // cloak, neck, rings, trinkets, shields, off-hand items
            }
        }

        // The heaviest armor the player has learned to wear.
        uint32 BestArmor(Player const* player)
        {
            if (player->HasSkill(SKILL_PLATE_MAIL))
                return ITEM_SUBCLASS_ARMOR_PLATE;
            if (player->HasSkill(SKILL_MAIL))
                return ITEM_SUBCLASS_ARMOR_MAIL;
            if (player->HasSkill(SKILL_LEATHER))
                return ITEM_SUBCLASS_ARMOR_LEATHER;
            return ITEM_SUBCLASS_ARMOR_CLOTH;
        }

        // A healer wears any armor it can (a holy paladin's cloth and mail healing pieces, as
        // Shared/RandomGear gives them): the score decides. Until 2026-11-28 only the heaviest
        // armor here, so a healer never put on a lighter healing piece it looted.
        bool Wearable(Player const* player, ItemPrototype const* proto, uint8 role)
        {
            if (!proto || (proto->Class != ITEM_CLASS_WEAPON && proto->Class != ITEM_CLASS_ARMOR))
                return false;
            if (player->CanUseItem(proto) != EQUIP_ERR_OK)
                return false;
            // Tools (Mining Pick, Skinning Knife) stay in the bags (Supplies counts them there), and
            // fishing poles and the like are not worn either (GearSources::IsOddity).
            if (GearSources::IsOddity(proto))
                return false;
            if (proto->Class == ITEM_CLASS_ARMOR && IsBodyArmor(proto->InventoryType) && role != ROLE_HEALER &&
                proto->SubClass != BestArmor(player))
                return false;
            return true;
        }

        // Scores the item level decides between (see the header).
        bool ScoredByItemLevel(ItemPrototype const* proto)
        {
            return proto->InventoryType == INVTYPE_TRINKET || proto->InventoryType == INVTYPE_RELIC;
        }

        bool IsShieldTank(uint8 classId, uint8 role)
        {
            return role == ROLE_TANK && (classId == CLASS_WARRIOR || classId == CLASS_PALADIN);
        }

        bool FightsWithWeapons(uint8 classId, uint8 role)
        {
            return classId == CLASS_WARRIOR || classId == CLASS_ROGUE || classId == CLASS_HUNTER ||
                   role == ROLE_MELEE_DPS || role == ROLE_TANK;
        }

        // 0.5 at weapon skill 0 .. 1 at the level's maximum.
        float SkillFactor(Player const* player, ItemPrototype const* proto, uint8 classId, uint8 role)
        {
            if (proto->Class != ITEM_CLASS_WEAPON || !FightsWithWeapons(classId, role))
                return 1.0f;
            uint32 const skill = proto->GetProficiencySkill();
            uint32 const max = player->GetSkillMaxForLevel();
            if (!skill || !max)
                return 1.0f;
            float const share = std::min(1.0f, float(player->GetSkillValue(uint16(skill))) / float(max));
            return 0.5f + 0.5f * share;
        }

        struct Context
        {
            Player const* player;
            uint8 classId;
            uint8 role;
            bool shieldTank;
            GearFit::Wearer wearer;
        };

        float ScoreOf(Context const& c, ItemPrototype const* proto, uint32 randomPropertyId)
        {
            float const score = randomPropertyId == UNKNOWN_SUFFIX
                ? GearFit::ExpectedScore(proto, c.wearer)
                : GearFit::Score(proto, c.wearer, randomPropertyId);
            return score * SkillFactor(c.player, proto, c.classId, c.role);
        }

        uint32 SuffixOf(Item const* item)
        {
            int32 const id = item->GetItemRandomPropertyId();
            return id > 0 ? uint32(id) : 0;
        }

        Item const* WornAt(Player const* player, uint8 slot)
        {
            return player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
        }

        float WornScore(Context const& c, uint8 slot)
        {
            Item const* item = WornAt(c.player, slot);
            return item ? ScoreOf(c, item->GetProto(), SuffixOf(item)) : 0.0f;
        }

        bool FitsOffHand(Context const& c, ItemPrototype const* proto)
        {
            switch (proto->InventoryType)
            {
                case INVTYPE_SHIELD:
                    return true;
                case INVTYPE_HOLDABLE:
                case INVTYPE_WEAPONOFFHAND:
                    return !c.shieldTank;
                case INVTYPE_WEAPON:
                    return !c.shieldTank && c.player->CanDualWield();
                default:
                    return false;
            }
        }

        // The best off hand in the bags (not `self`), 0 without one.
        float BestBagOffHand(Context const& c, Item const* self)
        {
            float best = 0.0f;
            for (Item const* item : Inventory::BagItems(c.player))
            {
                if (item == self)
                    continue;
                ItemPrototype const* proto = item->GetProto();
                if (!FitsOffHand(c, proto) || !Wearable(c.player, proto, c.role))
                    continue;
                best = std::max(best, ScoreOf(c, proto, SuffixOf(item)));
            }
            return best;
        }

        Upgrade EvaluateIn(Player const* player, ItemPrototype const* proto, uint32 randomPropertyId, Item const* self)
        {
            Upgrade out;
            if (!player)
                return out;

            Context c;
            c.player = player;
            c.classId = player->GetClass();
            c.role = RoleOf(player);
            if (!Wearable(player, proto, c.role))
                return out;
            c.shieldTank = IsShieldTank(c.classId, c.role);
            if (c.shieldTank && proto->InventoryType == INVTYPE_2HWEAPON)
                return out;
            c.wearer = GearFit::Of(player, c.role);

            float const score = ScoreOf(c, proto, randomPropertyId);
            uint8 slots[4];
            proto->GetAllowedEquipSlots(slots, c.classId, player->CanDualWield());
            for (uint8 slot : slots)
            {
                if (slot >= EQUIPMENT_SLOT_END)
                    continue;
                float mine = score;
                float replaced;
                if (slot == EQUIPMENT_SLOT_OFFHAND)
                {
                    if (player->IsTwoHandUsed() || !FitsOffHand(c, proto))
                        continue;
                    replaced = WornScore(c, slot);
                }
                else if (slot == EQUIPMENT_SLOT_MAINHAND)
                {
                    Item const* main = WornAt(player, EQUIPMENT_SLOT_MAINHAND);
                    replaced = WornScore(c, EQUIPMENT_SLOT_MAINHAND);
                    if (proto->InventoryType == INVTYPE_2HWEAPON)
                        replaced += WornScore(c, EQUIPMENT_SLOT_OFFHAND);
                    else if (main && main->GetProto()->InventoryType == INVTYPE_2HWEAPON)
                        mine += BestBagOffHand(c, self);
                }
                else
                    replaced = WornScore(c, slot);

                float gain = mine - replaced;
                if (mine <= 0.0f && replaced <= 0.0f && ScoredByItemLevel(proto))
                {
                    Item const* worn = WornAt(player, slot);
                    uint32 const wornLevel = worn ? worn->GetProto()->ItemLevel : 0;
                    gain = proto->ItemLevel > wornLevel ? float(proto->ItemLevel - wornLevel) * ITEM_LEVEL_POINT : 0.0f;
                }
                else if (mine <= 0.0f || gain <= replaced * MIN_GAIN_PCT / 100.0f)
                    continue;
                if (gain > out.gain)
                {
                    out.gain = gain;
                    out.slot = slot;
                    out.score = mine;
                    out.replaced = replaced;
                }
            }
            return out;
        }
    }

    void SetRole(ObjectGuid player, uint8 role)
    {
        std::lock_guard<std::mutex> guard(s_rolesLock);
        if (role == ROLE_INVALID)
            s_roles.erase(player.GetCounter());
        else
            s_roles[player.GetCounter()] = role;
    }

    uint8 RoleOf(Player const* player)
    {
        std::lock_guard<std::mutex> guard(s_rolesLock);
        auto const itr = s_roles.find(player->GetGUIDLow());
        return itr != s_roles.end() ? itr->second : uint8(ROLE_INVALID);
    }

    Upgrade Evaluate(Player const* player, ItemPrototype const* proto, uint32 randomPropertyId)
    {
        return EvaluateIn(player, proto, randomPropertyId, nullptr);
    }

    Upgrade Evaluate(Player const* player, Item const* item)
    {
        if (!item)
            return Upgrade();
        return EvaluateIn(player, item->GetProto(), SuffixOf(item), item);
    }

    float UpgradeGain(Player const* player, ItemPrototype const* proto, uint32 randomPropertyId)
    {
        return Evaluate(player, proto, randomPropertyId).gain;
    }

    bool IsUpgrade(Player const* player, ItemPrototype const* proto, uint32 randomPropertyId)
    {
        return UpgradeGain(player, proto, randomPropertyId) > 0.0f;
    }

    bool IsUpgrade(Player const* player, Item const* item)
    {
        return Evaluate(player, item).gain > 0.0f;
    }

    bool IsBagUpgrade(Player const* player, ItemPrototype const* proto)
    {
        if (!player || !proto || proto->Class != ITEM_CLASS_CONTAINER || proto->SubClass != ITEM_SUBCLASS_CONTAINER)
            return false;
        if (player->CanUseItem(proto) != EQUIP_ERR_OK)
            return false;

        for (uint8 slot = INVENTORY_SLOT_BAG_START; slot < INVENTORY_SLOT_BAG_END; ++slot)
        {
            Item const* bag = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
            if (!bag)
                return true;
            ItemPrototype const* worn = bag->GetProto();
            if (worn->Class == ITEM_CLASS_CONTAINER && worn->SubClass == ITEM_SUBCLASS_CONTAINER && worn->ContainerSlots < proto->ContainerSlots)
                return true;
        }
        return false;
    }
}
}
