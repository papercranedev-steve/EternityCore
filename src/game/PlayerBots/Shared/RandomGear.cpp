#include "Shared/RandomGear.h"
#include "Shared/GearFit.h"
#include "Shared/GearSources.h"
#include "Shared/Inventory.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "World.h"
#include "ItemPrototype.h"
#include "Utilities/Random.h"
#include <algorithm>
#include <map>
#include <random>

namespace BotShared
{
namespace RandomGear
{
    namespace
    {
        uint32 const SPELL_MAIL_PROFICIENCY = 8737;
        uint32 const SPELL_PLATE_PROFICIENCY = 750;
        uint32 const SPELL_DUAL_WIELD = 674;
        // EquipStage keeps no candidate below this share of the slot's best class-role fit.
        float const MIN_FIT_SHARE = 0.5f;

        bool IsShieldClass(uint8 classId)
        {
            return classId == CLASS_WARRIOR || classId == CLASS_PALADIN || classId == CLASS_SHAMAN;
        }

        uint32 PrimaryStat(uint8 classId, uint8 role)
        {
            switch (classId)
            {
                case CLASS_WARRIOR:
                    return ITEM_MOD_STRENGTH;
                case CLASS_PALADIN:
                    return role == ROLE_HEALER ? ITEM_MOD_INTELLECT : ITEM_MOD_STRENGTH;
                case CLASS_HUNTER:
                case CLASS_ROGUE:
                    return ITEM_MOD_AGILITY;
                case CLASS_SHAMAN:
                case CLASS_DRUID:
                    return (role == ROLE_MELEE_DPS || role == ROLE_TANK) ? ITEM_MOD_AGILITY : ITEM_MOD_INTELLECT;
                case CLASS_PRIEST:
                case CLASS_MAGE:
                case CLASS_WARLOCK:
                    return ITEM_MOD_INTELLECT;
            }
            return ITEM_MOD_STAMINA;
        }

        bool HasStat(ItemPrototype const* proto, uint32 stat)
        {
            for (auto const& s : proto->ItemStat)
                if (s.ItemStatType == stat && s.ItemStatValue > 0)
                    return true;
            return false;
        }

        // Rules any random gear follows, whatever the way (see the header).
        bool Wearable(Player* player, uint8 role, ItemPrototype const* proto, bool onlyPvE)
        {
            if (!proto->Discovered || proto->HasExtraFlag(ITEM_EXTRA_NOT_OBTAINABLE))
                return false;
            if (proto->Class != ITEM_CLASS_WEAPON && proto->Class != ITEM_CLASS_ARMOR)
                return false;
            if (proto->InventoryType == INVTYPE_TABARD || proto->InventoryType == INVTYPE_BODY)
                return false;
            if (GearSources::IsOddity(proto))
                return false;
            // No level requirement but far above our level (quest rewards like the fishing contest
            // or arena prizes): a player of this level would not have it.
            if (!proto->RequiredLevel && proto->ItemLevel > player->GetLevel() + 10)
                return false;
            if (proto->SourceQuestRaces && !(proto->SourceQuestRaces & player->GetRaceMask()))
                return false;
            if (proto->SourceQuestClasses && !(proto->SourceQuestClasses & player->GetClassMask()))
                return false;
            if (proto->SourceQuestLevel < 0)
            {
                // Avoid higher level items with no level requirement
                if (!proto->RequiredLevel && proto->ItemLevel > player->GetLevel())
                    return false;
            }
            else if (uint32(proto->SourceQuestLevel) > player->GetLevel())
                return false;   // from a higher level quest
            if (player->CanUseItem(proto, onlyPvE) != EQUIP_ERR_OK)
                return false;
            if (proto->RequiredReputationFaction &&
                uint32(player->GetReputationRank(proto->RequiredReputationFaction)) < proto->RequiredReputationRank)
                return false;
            if (uint32 skill = proto->GetProficiencySkill())
            {
                // Don't equip cloth items on warriors, etc unless bot is a healer
                if (proto->Class == ITEM_CLASS_ARMOR &&
                    proto->InventoryType != INVTYPE_CLOAK &&
                    proto->InventoryType != INVTYPE_SHIELD &&
                    skill != player->GetHighestKnownArmorProficiency() &&
                    role != ROLE_HEALER)
                    return false;
                // Fist weapons use unarmed skill calculations, but we must query fist weapon skill presence to use this item
                if (proto->SubClass == ITEM_SUBCLASS_WEAPON_FIST)
                    skill = SKILL_FIST_WEAPONS;
                if (!player->GetSkillValue(skill))
                    return false;
            }
            return true;
        }

        bool IsCosmetic(ItemPrototype const* proto)
        {
            // Festival and costume pieces that carry a token bit of armor, so the rule below misses them.
            switch (proto->ItemId)
            {
                case 23323: case 23324:     // Crown / Mantle of the Fire Festival
                case 21524: case 21525:     // Red / Green Winter Hat
                case 6836:                  // Dress Shoes
                    return true;
                default:
                    break;
            }
            // Dresses, tuxedos and the like: armor slot items without any armor.
            if (proto->Class == ITEM_CLASS_ARMOR && !proto->Armor)
            {
                switch (proto->InventoryType)
                {
                    case INVTYPE_HEAD: case INVTYPE_SHOULDERS: case INVTYPE_CHEST: case INVTYPE_ROBE:
                    case INVTYPE_WAIST: case INVTYPE_LEGS: case INVTYPE_FEET: case INVTYPE_WRISTS:
                    case INVTYPE_HANDS: case INVTYPE_CLOAK:
                        return true;
                    default:
                        break;
                }
            }
            return false;
        }

        // Adds `proto` to the empty slots it may go in.
        void AddToSlots(Player* player, uint8 role, ItemPrototype const* proto, uint32 skipSlotMask,
            std::map<uint32, std::vector<ItemPrototype const*>>& itemsPerSlot)
        {
            uint8 slots[4];
            proto->GetAllowedEquipSlots(slots, player->GetClass(), player->CanDualWield());
            for (uint8 slot : slots)
            {
                if (slot < EQUIPMENT_SLOT_START || slot >= EQUIPMENT_SLOT_END ||
                    (skipSlotMask & (1u << slot)) || player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
                    continue;
                if (slot == EQUIPMENT_SLOT_OFFHAND)
                {
                    // Only allow shield in offhand for tanks
                    if (proto->InventoryType != INVTYPE_SHIELD && role == ROLE_TANK && IsShieldClass(player->GetClass()))
                        continue;
                    // Only equip holdables on mana users
                    if (proto->InventoryType == INVTYPE_HOLDABLE && role != ROLE_HEALER && role != ROLE_RANGE_DPS)
                        continue;
                }
                itemsPerSlot[slot].push_back(proto);
                // Unique item
                if (proto->MaxCount == 1)
                    break;
            }
        }

        bool TwoHanderInMainHand(Player* player)
        {
            Item* mainHand = player->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_MAINHAND);
            return mainHand && mainHand->GetProto()->InventoryType == INVTYPE_2HWEAPON;
        }

        uint32 Lerp(uint32 low, uint32 high, uint32 strength)
        {
            return uint32((int64(low) * (100 - strength) + int64(high) * strength) / 100);
        }

        bool DropsInAny(uint32 itemId, std::vector<uint32> const& maps)
        {
            for (uint32 mapId : GearSources::InstanceMapsOf(itemId))
                if (std::find(maps.begin(), maps.end(), mapId) != maps.end())
                    return true;
            return false;
        }

        uint32 QualityIndex(ItemPrototype const* proto)
        {
            return std::min<uint32>(proto->Quality, GearStages::QUALITIES - 1);
        }
    }

    void LearnArmorProficiencies(Player* player)
    {
        if (player->GetLevel() < 40)
            return;
        switch (player->GetClass())
        {
            case CLASS_WARRIOR:
            case CLASS_PALADIN:
                if (!player->HasSpell(SPELL_PLATE_PROFICIENCY))
                    player->LearnSpell(SPELL_PLATE_PROFICIENCY, false, false);
                break;
            case CLASS_HUNTER:
            case CLASS_SHAMAN:
                if (!player->HasSpell(SPELL_MAIL_PROFICIENCY))
                    player->LearnSpell(SPELL_MAIL_PROFICIENCY, false, false);
                break;
        }
    }

    void FillEmptySlots(Player* player, uint8 role, RandomGearOptions const& options)
    {
        LearnArmorProficiencies(player);

        // Seeded picks give the same gear for the same seed (a roster member looks the same every time).
        std::mt19937 rng(options.seed);
        auto roll = [&options, &rng](uint32 count) -> uint32   // [0, count)
        {
            if (count <= 1)
                return 0;
            return options.seed ? std::uniform_int_distribution<uint32>(0, count - 1)(rng) : urand(0, count - 1);
        };

        bool const onlyPvE = !options.allowPvP || urand(0, 1) != 0;
        uint8 const honorRank = onlyPvE ? 0 : urand(5, 18);

        bool weighted = false;
        for (uint32 w : options.qualityWeights)
            weighted = weighted || w > 0;

        std::map<uint32 /*slot*/, std::vector<ItemPrototype const*>> itemsPerSlot;
        for (auto const& itr : sObjectMgr.GetItemPrototypeMap())
        {
            ItemPrototype const* pProto = &itr.second;
            if (!Wearable(player, role, pProto, onlyPvE))
                continue;

            if (pProto->InventoryType == INVTYPE_TRINKET &&
                (player->GetLevel() < options.minTrinketLevel || GearSources::IsCrafted(pProto->ItemId)))
                continue;

            // Avoid low level items
            if ((pProto->ItemLevel + sWorld.getConfig(CONFIG_UINT32_PARTY_BOT_RANDOM_GEAR_LEVEL_DIFFERENCE)) < player->GetLevel())
                continue;

            if (options.maxItemLevel && pProto->ItemLevel > options.maxItemLevel)
                continue;

            if (!options.allowPvP && pProto->RequiredHonorRank)
                continue;

            if (!options.allowCosmetic && IsCosmetic(pProto))
                continue;

            if (pProto->RequiredHonorRank > honorRank)
                continue;

            AddToSlots(player, role, pProto, options.skipSlotMask, itemsPerSlot);
        }

        // 1. Remove items that don't have our primary stat from the list
        // 2. Remove non-pvp items if we have a pvp item available
        uint32 const primaryStat = PrimaryStat(player->GetClass(), role);
        for (auto& itr : itemsPerSlot)
        {
            // 0. Quality first (before the primary stat filter, which would drop most white items).
            if (weighted && !itr.second.empty())
            {
                uint32 total = 0;
                uint32 weightOf[MAX_ITEM_QUALITY] = {};
                for (ItemPrototype const* pItem : itr.second)
                    if (pItem->Quality < MAX_ITEM_QUALITY && !weightOf[pItem->Quality])
                        total += (weightOf[pItem->Quality] = options.qualityWeights[pItem->Quality]);

                if (!total)
                {
                    itr.second.clear();     // only qualities we don't want: leave the slot empty
                    continue;
                }

                uint32 pick = roll(total);
                uint32 quality = 0;
                for (; quality < MAX_ITEM_QUALITY; ++quality)
                {
                    if (pick < weightOf[quality])
                        break;
                    pick -= weightOf[quality];
                }
                itr.second.erase(std::remove_if(itr.second.begin(), itr.second.end(),
                    [quality](ItemPrototype const* pItem) { return pItem->Quality != quality; }), itr.second.end());
            }

            bool hasPrimaryStatItem = false;
            for (auto const& pItem : itr.second)
                if ((hasPrimaryStatItem = HasStat(pItem, primaryStat)))
                    break;

            if (hasPrimaryStatItem)
                itr.second.erase(std::remove_if(itr.second.begin(), itr.second.end(),
                    [primaryStat](ItemPrototype const* pItem) { return !HasStat(pItem, primaryStat); }),
                    itr.second.end());

            if (!options.allowPvP)
                continue;

            bool hasPvpItem = false;
            for (auto const& pItem : itr.second)
                if ((hasPvpItem = pItem->RequiredHonorRank != 0))
                    break;

            if (hasPvpItem)
                itr.second.erase(std::remove_if(itr.second.begin(), itr.second.end(),
                    [](ItemPrototype const* pItem) { return pItem->RequiredHonorRank == 0; }),
                    itr.second.end());
        }

        for (auto const& itr : itemsPerSlot)
        {
            // Don't equip offhand if using 2 handed weapon
            if (itr.first == EQUIPMENT_SLOT_OFFHAND && TwoHanderInMainHand(player))
                continue;

            if (itr.second.empty())
                continue;

            ItemPrototype const* pProto = options.seed ? itr.second[roll(uint32(itr.second.size()))]
                                                       : SelectRandomContainerElement(itr.second);
            if (!pProto)
                continue;

            player->SatisfyItemRequirements(pProto);
            player->StoreNewItemInBestSlots(pProto->ItemId, 1);
        }

        // Armor slots may stay empty, the shirt never does (always a plain one, see the helper).
        Inventory::WearPlainShirt(player, roll(3));
    }

    uint32 EquipStage(Player* player, uint8 role, StageRequest const& request)
    {
        GearStages::StageDef const& stage = GearStages::Get(request.stage);
        uint32 const strength = std::min<uint32>(request.strength, 100);
        uint32 const level = player->GetLevel();
        uint8 const classId = player->GetClass();

        LearnArmorProficiencies(player);
        // Dual wield from the trainer: rogues at 10, warriors and hunters at 20.
        if (!player->HasSpell(SPELL_DUAL_WIELD) &&
            ((classId == CLASS_ROGUE && level >= 10) || ((classId == CLASS_WARRIOR || classId == CLASS_HUNTER) && level >= 20)))
            player->LearnSpell(SPELL_DUAL_WIELD, false, false);

        std::mt19937 rng(request.seed ? request.seed : urand(1, 0x7fffffff));
        auto roll = [&rng](uint32 count) -> uint32   // [0, count)
        {
            return count <= 1 ? 0 : std::uniform_int_distribution<uint32>(0, count - 1)(rng);
        };
        auto chance = [&roll](int32 pct) { return pct > 0 && int32(roll(100)) < pct; };

        // The mix at this strength.
        uint32 qualityWeight[GearStages::QUALITIES];
        uint32 sourceWeight[GearSources::SOURCE_COUNT];
        for (uint32 q = 0; q < GearStages::QUALITIES; ++q)
            qualityWeight[q] = Lerp(stage.low.quality[q], stage.high.quality[q], strength);
        if (level >= stage.commonUntilLevel)
            qualityWeight[ITEM_QUALITY_NORMAL] = 0;
        for (uint32 s = 0; s < GearSources::SOURCE_COUNT; ++s)
            sourceWeight[s] = Lerp(stage.low.source[s], stage.high.source[s], strength);

        // Window; the stronger end of the stage reaches up to two levels further.
        int32 const above = stage.itemLevelAbove + int32(strength / 50);
        uint32 const minItemLevel = uint32(std::max<int32>(int32(level) + stage.itemLevelBelow, 1));
        uint32 maxItemLevel = uint32(std::max<int32>(int32(level) + above, 1));
        if (request.maxItemLevel)
            maxItemLevel = std::min(maxItemLevel, request.maxItemLevel);

        // Slots a player of this level often has nothing in yet.
        uint32 skipSlotMask = request.skipSlotMask;
        int32 const jewelryPct = stage.jewelryEmptyBase - stage.jewelryEmptyPerLevel10 * int32(level) / 10;
        for (uint32 slot : { EQUIPMENT_SLOT_NECK, EQUIPMENT_SLOT_FINGER1, EQUIPMENT_SLOT_FINGER2,
                             EQUIPMENT_SLOT_TRINKET1, EQUIPMENT_SLOT_TRINKET2 })
            if (chance(jewelryPct))
                skipSlotMask |= 1u << slot;
        int32 const minorPct = stage.minorEmptyBase - stage.minorEmptyPerLevel10 * int32(level) / 10;
        for (uint32 slot : { EQUIPMENT_SLOT_BACK, EQUIPMENT_SLOT_WRISTS, EQUIPMENT_SLOT_SHOULDERS })
            if (chance(minorPct))
                skipSlotMask |= 1u << slot;

        bool const shieldTank = role == ROLE_TANK && IsShieldClass(classId);
        GearFit::Wearer const wearer = GearFit::Of(player, role);
        uint32 const PVP_SOURCE_INDEX = 6;  // GearSources::SOURCE_PVP = 1 << 6
        bool const wantsPvP = sourceWeight[PVP_SOURCE_INDEX] > 0;

        std::map<uint32, std::vector<ItemPrototype const*>> itemsPerSlot;
        std::map<uint32, uint32> sourcesOf;     // item -> usable source bits
        for (auto const& itr : sObjectMgr.GetItemPrototypeMap())
        {
            ItemPrototype const* proto = &itr.second;
            if (proto->ItemLevel < minItemLevel || proto->ItemLevel > maxItemLevel)
                continue;
            if (proto->Quality >= GearStages::QUALITIES || !qualityWeight[proto->Quality])
                continue;
            if (proto->RequiredHonorRank && !wantsPvP)
                continue;
            if (proto->InventoryType == INVTYPE_TRINKET &&
                (level < stage.minTrinketLevel || GearSources::IsCrafted(proto->ItemId)))
                continue;
            // A shield class tank holds a one hander and a shield.
            if (shieldTank && proto->InventoryType == INVTYPE_2HWEAPON)
                continue;
            if (IsCosmetic(proto))
                continue;
            if (!Wearable(player, role, proto, !wantsPvP))
                continue;

            uint32 sources = GearSources::SourcesOf(proto);
            if ((sources & GearSources::SOURCE_RAID) && !DropsInAny(proto->ItemId, stage.raidMaps))
                sources &= ~uint32(GearSources::SOURCE_RAID);
            uint32 usable = 0;
            for (uint32 s = 0; s < GearSources::SOURCE_COUNT; ++s)
                if ((sources & (1u << s)) && sourceWeight[s])
                    usable |= 1u << s;
            if (!usable)
                continue;
            sourcesOf[proto->ItemId] = usable;
            AddToSlots(player, role, proto, skipSlotMask, itemsPerSlot);
        }

        uint32 worn = 0;
        for (auto& itr : itemsPerSlot)
        {
            if (itr.first == EQUIPMENT_SLOT_OFFHAND && TwoHanderInMainHand(player))
                continue;
            // the slot may have been filled by an earlier pick (a ring that went to the other finger)
            if (player->GetItemByPos(INVENTORY_SLOT_BAG_0, uint8(itr.first)))
                continue;
            std::vector<ItemPrototype const*>& candidates = itr.second;
            if (candidates.empty())
                continue;

            // 1. quality x source, among what this slot has
            std::map<std::pair<uint32, uint32>, uint32> pairs;
            uint32 total = 0;
            for (ItemPrototype const* proto : candidates)
            {
                uint32 const q = QualityIndex(proto);
                uint32 const bits = sourcesOf[proto->ItemId];
                for (uint32 s = 0; s < GearSources::SOURCE_COUNT; ++s)
                    if ((bits & (1u << s)) && !pairs.count({ q, s }))
                        total += (pairs[{ q, s }] = qualityWeight[q] * sourceWeight[s]);
            }
            if (!total)
                continue;
            uint32 pick = roll(total);
            std::pair<uint32, uint32> chosen = pairs.begin()->first;
            for (auto const& p : pairs)
            {
                if (pick < p.second)
                {
                    chosen = p.first;
                    break;
                }
                pick -= p.second;
            }
            std::vector<std::pair<float, ItemPrototype const*>> scored;
            for (ItemPrototype const* proto : candidates)
                if (QualityIndex(proto) == chosen.first && (sourcesOf[proto->ItemId] & (1u << chosen.second)))
                    scored.emplace_back(GearFit::BestScore(proto, wearer), proto);
            if (scored.empty())
                continue;

            // 2. the better fitting half, none below half the best fit (in a slot with few
            // fitting items the half still held a mage's strength necklace), what fits
            // nothing only when nothing fits
            std::sort(scored.begin(), scored.end(), [](auto const& a, auto const& b)
            {
                return a.first != b.first ? a.first > b.first : a.second->ItemId < b.second->ItemId;
            });
            size_t keep = (scored.size() + 1) / 2;
            float const fitFloor = scored.front().first * MIN_FIT_SHARE;
            if (scored.front().first > 0.0f)
                while (keep > 1 && scored[keep - 1].first < fitFloor)
                    --keep;
            ItemPrototype const* proto = scored[roll(uint32(keep))].second;

            player->SatisfyItemRequirements(proto);
            uint32 const suffix = GearFit::BestRandomProperty(proto, wearer, roll(1000));
            if (player->StoreNewItemInBestSlots(proto->ItemId, 1, 0, suffix))
                ++worn;
        }

        Inventory::WearPlainShirt(player, roll(3));
        return worn;
    }

    void StripEquipment(Player* player)
    {
        for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
            if (player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
                player->DestroyItem(INVENTORY_SLOT_BAG_0, slot, true);
    }
}
}
