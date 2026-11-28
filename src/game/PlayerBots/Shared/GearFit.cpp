#include "Shared/GearFit.h"
#include "ItemPrototype.h"
#include "ItemEnchantmentMgr.h"
#include "SpellMgr.h"
#include "SharedDefines.h"
#include "Spells/SpellAuraDefines.h"
#include "Spells/SpellDefines.h"
#include "DBCStores.h"
#include <algorithm>
#include <vector>

namespace BotShared
{
namespace GearFit
{
    namespace
    {
        struct Stats
        {
            float str = 0, agi = 0, sta = 0, intel = 0, spi = 0;
            float armor = 0, ap = 0, rap = 0;
            float spell = 0;            // spell damage of the class's schools (weighed by overlap)
            float healing = 0, mp5 = 0;
            float hit = 0, spellHit = 0, crit = 0, spellCrit = 0, weaponSkill = 0;
            float dodge = 0, parry = 0, block = 0, blockValue = 0, defense = 0;
            float meleeDps = 0, rangedDps = 0;
        };

        struct Weights
        {
            float str, agi, sta, intel, spi;
            float armor, ap, rap;
            float spell, healing, mp5;
            float hit, spellHit, crit, spellCrit, weaponSkill;
            float dodge, parry, block, blockValue, defense;
            float meleeDps, rangedDps;
        };

        // Armor counts a little for everyone and weapon damage a little for casters (they swing
        // too, at low level and out of mana): a plain white robe or staff beats an empty slot
        // (Shared/GearUpgrade compares by these scores).
        //                                str   agi   sta   int   spi   armor  ap    rap   spell heal  mp5   hit spHit crit spCrit wSkill dodge parry block bVal  def   mDps rDps
        Weights const TANK_PLATE      = { 0.5f, 0.5f, 1.0f, 0.0f, 0.0f, 0.02f, 0.1f, 0.0f, 0.0f, 0.0f, 0.0f, 5,  0,    0,   0,     1.0f,  10,   10,   5,    0.3f, 1.2f, 0.5f, 0.1f };
        Weights const TANK_BEAR       = { 0.4f, 0.8f, 1.0f, 0.0f, 0.0f, 0.03f, 0.15f,0.0f, 0.0f, 0.0f, 0.0f, 5,  0,    4,   0,     0.0f,  10,   0,    0,    0.0f, 1.0f, 0.0f, 0.0f };
        Weights const MELEE_STRENGTH  = { 1.0f, 0.6f, 0.25f,0.0f, 0.0f, 0.01f, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 12, 0,    12,  0,     2.0f,  0,    0,    0,    0.0f, 0.0f, 3.0f, 0.2f };
        Weights const MELEE_HYBRID    = { 1.0f, 0.5f, 0.25f,0.2f, 0.0f, 0.01f, 0.5f, 0.0f, 0.1f, 0.0f, 0.1f, 12, 0,    12,  0,     2.0f,  0,    0,    0,    0.0f, 0.0f, 3.0f, 0.0f };
        Weights const MELEE_AGILITY   = { 0.5f, 1.0f, 0.25f,0.0f, 0.0f, 0.01f, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 14, 0,    12,  0,     2.0f,  0,    0,    0,    0.0f, 0.0f, 3.0f, 0.2f };
        Weights const MELEE_CAT       = { 0.7f, 1.0f, 0.25f,0.0f, 0.0f, 0.01f, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 10, 0,    12,  0,     0.0f,  0,    0,    0,    0.0f, 0.0f, 0.0f, 0.0f };
        Weights const RANGED_HUNTER   = { 0.05f,1.0f, 0.25f,0.3f, 0.0f, 0.01f, 0.05f,0.45f,0.0f, 0.0f, 0.1f, 12, 0,    12,  0,     1.0f,  0,    0,    0,    0.0f, 0.0f, 0.3f, 3.0f };
        Weights const CASTER          = { 0.0f, 0.0f, 0.3f, 0.5f, 0.1f, 0.01f, 0.0f, 0.0f, 1.0f, 0.0f, 0.5f, 0,  10,   0,   9,     0.0f,  0,    0,    0,    0.0f, 0.0f, 0.1f, 0.4f };
        Weights const CASTER_WARLOCK  = { 0.0f, 0.0f, 0.45f,0.4f, 0.1f, 0.01f, 0.0f, 0.0f, 1.0f, 0.0f, 0.4f, 0,  10,   0,   8,     0.0f,  0,    0,    0,    0.0f, 0.0f, 0.1f, 0.4f };
        Weights const HEALER          = { 0.0f, 0.0f, 0.25f,0.7f, 0.4f, 0.01f, 0.0f, 0.0f, 0.0f, 0.9f, 1.2f, 0,  0,    0,   5,     0.0f,  0,    0,    0,    0.0f, 0.0f, 0.1f, 0.2f };
        Weights const HEALER_PALADIN  = { 0.0f, 0.0f, 0.25f,1.0f, 0.1f, 0.01f, 0.0f, 0.0f, 0.0f, 0.9f, 1.2f, 0,  0,    0,   6,     0.0f,  0,    0,    0,    0.0f, 0.0f, 0.2f, 0.0f };

        Weights const& WeightsFor(uint8 classId, uint8 role)
        {
            if (role == ROLE_INVALID)
            {
                switch (classId)
                {
                    case CLASS_WARRIOR: case CLASS_ROGUE: role = ROLE_MELEE_DPS; break;
                    case CLASS_HUNTER: case CLASS_MAGE: case CLASS_WARLOCK: role = ROLE_RANGE_DPS; break;
                    default: role = ROLE_HEALER; break;
                }
            }
            switch (role)
            {
                case ROLE_TANK:
                    if (classId == CLASS_DRUID)
                        return TANK_BEAR;
                    if (classId == CLASS_WARRIOR || classId == CLASS_PALADIN)
                        return TANK_PLATE;
                    break;
                case ROLE_HEALER:
                    if (classId == CLASS_PALADIN)
                        return HEALER_PALADIN;
                    if (classId == CLASS_PRIEST || classId == CLASS_DRUID || classId == CLASS_SHAMAN)
                        return HEALER;
                    break;
                case ROLE_MELEE_DPS:
                    switch (classId)
                    {
                        case CLASS_WARRIOR: return MELEE_STRENGTH;
                        case CLASS_PALADIN: case CLASS_SHAMAN: return MELEE_HYBRID;
                        case CLASS_ROGUE: return MELEE_AGILITY;
                        case CLASS_DRUID: return MELEE_CAT;
                        case CLASS_HUNTER: return RANGED_HUNTER;
                        default: break;
                    }
                    break;
                case ROLE_RANGE_DPS:
                    if (classId == CLASS_HUNTER)
                        return RANGED_HUNTER;
                    break;
                default:
                    break;
            }
            // what the class does otherwise
            switch (classId)
            {
                case CLASS_WARRIOR: return MELEE_STRENGTH;
                case CLASS_ROGUE: return MELEE_AGILITY;
                case CLASS_HUNTER: return RANGED_HUNTER;
                case CLASS_WARLOCK: return CASTER_WARLOCK;
                case CLASS_PALADIN: return role == ROLE_RANGE_DPS ? MELEE_HYBRID : HEALER_PALADIN;
                default: return CASTER;
            }
        }

        // The schools a caster of the class damages with.
        uint32 SchoolsOf(uint8 classId)
        {
            switch (classId)
            {
                case CLASS_MAGE: return SPELL_SCHOOL_MASK_FIRE | SPELL_SCHOOL_MASK_FROST | SPELL_SCHOOL_MASK_ARCANE;
                case CLASS_WARLOCK: return SPELL_SCHOOL_MASK_SHADOW | SPELL_SCHOOL_MASK_FIRE;
                case CLASS_PRIEST: return SPELL_SCHOOL_MASK_SHADOW | SPELL_SCHOOL_MASK_HOLY;
                case CLASS_SHAMAN: return SPELL_SCHOOL_MASK_NATURE | SPELL_SCHOOL_MASK_FIRE | SPELL_SCHOOL_MASK_FROST;
                case CLASS_DRUID: return SPELL_SCHOOL_MASK_NATURE | SPELL_SCHOOL_MASK_ARCANE;
                case CLASS_PALADIN: return SPELL_SCHOOL_MASK_HOLY;
                default: return 0;
            }
        }

        // Share of the class's schools a "+damage to <schools>" bonus covers: 1 all of them,
        // 0.6 some, 0 none.
        float SchoolShare(uint32 bonusMask, uint32 classSchools)
        {
            uint32 const covered = bonusMask & classSchools;
            if (!covered)
                return 0.0f;
            return covered == classSchools ? 1.0f : 0.6f;
        }

        void AddItemMod(Stats& s, uint32 type, float value)
        {
            switch (type)
            {
                case ITEM_MOD_STRENGTH: s.str += value; break;
                case ITEM_MOD_AGILITY: s.agi += value; break;
                case ITEM_MOD_STAMINA: s.sta += value; break;
                case ITEM_MOD_INTELLECT: s.intel += value; break;
                case ITEM_MOD_SPIRIT: s.spi += value; break;
                case ITEM_MOD_HEALTH: s.sta += value / 10.0f; break;
                case ITEM_MOD_MANA: s.intel += value / 15.0f; break;
                default: break;
            }
        }

        // scale: share of the time the aura is up (1 for equip effects, see OnUseUptime).
        void AddEquipSpell(Stats& s, uint32 spellId, uint32 classSchools, float scale = 1.0f)
        {
            SpellEntry const* spell = spellId ? sSpellMgr.GetSpellEntry(spellId) : nullptr;
            if (!spell)
                return;
            for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
            {
                if (spell->Effect[i] != SPELL_EFFECT_APPLY_AURA)
                    continue;
                float const amount = float(spell->CalculateSimpleValue(SpellEffectIndex(i))) * scale;
                int32 const misc = spell->EffectMiscValue[i];
                switch (spell->EffectApplyAuraName[i])
                {
                    // 1.12 random suffixes ("of the Bear": +Strength, +Stamina) are equip spells
                    // of this aura, not stat enchantments.
                    case SPELL_AURA_MOD_STAT:
                        if (misc < 0 || misc == STAT_STRENGTH) s.str += amount;
                        if (misc < 0 || misc == STAT_AGILITY) s.agi += amount;
                        if (misc < 0 || misc == STAT_STAMINA) s.sta += amount;
                        if (misc < 0 || misc == STAT_INTELLECT) s.intel += amount;
                        if (misc < 0 || misc == STAT_SPIRIT) s.spi += amount;
                        break;
                    case SPELL_AURA_MOD_DAMAGE_DONE:
                        s.spell += amount * SchoolShare(uint32(misc), classSchools);
                        break;
                    case SPELL_AURA_MOD_HEALING_DONE: s.healing += amount; break;
                    case SPELL_AURA_MOD_ATTACK_POWER: s.ap += amount; break;
                    case SPELL_AURA_MOD_RANGED_ATTACK_POWER: s.rap += amount; break;
                    case SPELL_AURA_MOD_HIT_CHANCE: s.hit += amount; break;
                    case SPELL_AURA_MOD_SPELL_HIT_CHANCE: s.spellHit += amount; break;
                    case SPELL_AURA_MOD_CRIT_PERCENT: s.crit += amount; break;
                    case SPELL_AURA_MOD_SPELL_CRIT_CHANCE: s.spellCrit += amount; break;
                    case SPELL_AURA_MOD_DODGE_PERCENT: s.dodge += amount; break;
                    case SPELL_AURA_MOD_PARRY_PERCENT: s.parry += amount; break;
                    case SPELL_AURA_MOD_BLOCK_PERCENT: s.block += amount; break;
                    case SPELL_AURA_MOD_SHIELD_BLOCKVALUE: s.blockValue += amount; break;
                    case SPELL_AURA_MOD_SKILL:
                        if (misc == SKILL_DEFENSE)
                            s.defense += amount;
                        else
                            s.weaponSkill += amount;
                        break;
                    case SPELL_AURA_MOD_POWER_REGEN:
                        if (misc == POWER_MANA)
                            s.mp5 += amount;
                        break;
                    case SPELL_AURA_MOD_RESISTANCE:
                        if (misc & SPELL_SCHOOL_MASK_NORMAL)
                            s.armor += amount;
                        break;
                    default:
                        break;
                }
            }
        }

        void AddRandomProperty(Stats& s, uint32 randomPropertyId, uint32 classSchools)
        {
            ItemRandomPropertiesEntry const* prop = randomPropertyId ? sItemRandomPropertiesStore.LookupEntry(randomPropertyId) : nullptr;
            if (!prop)
                return;
            for (uint32 enchantId : prop->enchant_id)
            {
                SpellItemEnchantmentEntry const* ench = enchantId ? sSpellItemEnchantmentStore.LookupEntry(enchantId) : nullptr;
                if (!ench)
                    continue;
                for (uint32 i = 0; i < 3; ++i)
                {
                    switch (ench->type[i])
                    {
                        case ITEM_ENCHANTMENT_TYPE_STAT:
                            AddItemMod(s, ench->spellid[i], float(ench->amount[i]));
                            break;
                        case ITEM_ENCHANTMENT_TYPE_EQUIP_SPELL:
                            AddEquipSpell(s, ench->spellid[i], classSchools);
                            break;
                        case ITEM_ENCHANTMENT_TYPE_RESISTANCE:
                            if (ench->spellid[i] == SPELL_SCHOOL_NORMAL)
                                s.armor += float(ench->amount[i]);
                            break;
                        default:
                            break;
                    }
                }
            }
        }

        float WeaponDps(ItemPrototype const* proto)
        {
            if (proto->Class != ITEM_CLASS_WEAPON || !proto->Delay)
                return 0.0f;
            float damage = 0.0f;
            for (auto const& d : proto->Damage)
                damage += (d.DamageMin + d.DamageMax) / 2.0f;
            return damage * 1000.0f / float(proto->Delay);
        }

        // Share of a fight an on-use effect is up: its duration over its cooldown (Talisman of
        // Ephemeral Power: 15 s every 90 s). Without it caster trinkets, which are mostly on-use,
        // scored 0 and lost to tank trinkets with a little armor or stamina.
        float OnUseUptime(_ItemSpell const& itemSpell)
        {
            SpellEntry const* spell = itemSpell.SpellId ? sSpellMgr.GetSpellEntry(itemSpell.SpellId) : nullptr;
            if (!spell)
                return 0.0f;
            int32 const duration = spell->GetDuration();
            if (duration <= 0)
                return 0.0f;
            int32 cooldown = std::max(itemSpell.SpellCooldown, itemSpell.SpellCategoryCooldown);
            if (cooldown <= 0)
                cooldown = int32(spell->GetRecoveryTime());
            if (cooldown <= duration)
                return 1.0f;
            return float(duration) / float(cooldown);
        }

        Stats StatsOf(ItemPrototype const* proto, uint8 classId, uint32 randomPropertyId)
        {
            Stats s;
            uint32 const schools = SchoolsOf(classId);
            for (auto const& stat : proto->ItemStat)
                AddItemMod(s, stat.ItemStatType, float(stat.ItemStatValue));
            for (auto const& itemSpell : proto->Spells)
            {
                if (itemSpell.SpellTrigger == ITEM_SPELLTRIGGER_ON_EQUIP)
                    AddEquipSpell(s, itemSpell.SpellId, schools);
                else if (itemSpell.SpellTrigger == ITEM_SPELLTRIGGER_ON_USE)
                    AddEquipSpell(s, itemSpell.SpellId, schools, OnUseUptime(itemSpell));
            }
            AddRandomProperty(s, randomPropertyId, schools);
            s.armor += float(proto->Armor);
            s.blockValue += float(proto->Block);
            float const dps = WeaponDps(proto);
            if (proto->IsRangedWeapon())
                s.rangedDps += dps;
            else
                s.meleeDps += dps;
            return s;
        }
    }

    float Score(ItemPrototype const* proto, uint8 classId, uint8 role, uint32 randomPropertyId)
    {
        Weights const& w = WeightsFor(classId, role);
        Stats const s = StatsOf(proto, classId, randomPropertyId);
        float const score =
            w.str * s.str + w.agi * s.agi + w.sta * s.sta + w.intel * s.intel + w.spi * s.spi +
            w.armor * s.armor + w.ap * s.ap + w.rap * s.rap +
            w.spell * s.spell + w.healing * s.healing + w.mp5 * s.mp5 +
            w.hit * s.hit + w.spellHit * s.spellHit + w.crit * s.crit + w.spellCrit * s.spellCrit +
            w.weaponSkill * s.weaponSkill +
            w.dodge * s.dodge + w.parry * s.parry + w.block * s.block + w.blockValue * s.blockValue +
            w.defense * s.defense + w.meleeDps * s.meleeDps + w.rangedDps * s.rangedDps;
        return score > 0.0f ? score : 0.0f;
    }

    uint32 BestRandomProperty(ItemPrototype const* proto, uint8 classId, uint8 role, uint32 roll)
    {
        if (!proto->RandomProperty)
            return 0;
        std::vector<uint32> best;
        float bestScore = -1.0f;
        for (uint32 prop : GetItemEnchantModChoices(proto->RandomProperty))
        {
            if (!sItemRandomPropertiesStore.LookupEntry(prop))
                continue;
            float const score = Score(proto, classId, role, prop);
            if (score > bestScore + 0.01f)
            {
                bestScore = score;
                best.clear();
            }
            if (score > bestScore - 0.01f)
                best.push_back(prop);
        }
        return best.empty() ? 0 : best[roll % best.size()];
    }

    float BestScore(ItemPrototype const* proto, uint8 classId, uint8 role)
    {
        return Score(proto, classId, role, BestRandomProperty(proto, classId, role));
    }

    float ExpectedScore(ItemPrototype const* proto, uint8 classId, uint8 role)
    {
        if (!proto->RandomProperty)
            return Score(proto, classId, role);
        float sum = 0.0f;
        uint32 count = 0;
        for (uint32 prop : GetItemEnchantModChoices(proto->RandomProperty))
        {
            if (!sItemRandomPropertiesStore.LookupEntry(prop))
                continue;
            sum += Score(proto, classId, role, prop);
            ++count;
        }
        return count ? sum / float(count) : Score(proto, classId, role);
    }
}
}
