/*
 * Companions: the character sheet of one companion, a page at a time (the addon's companion
 * window, addons/PartyBot/CompanionSheet.lua). Online companions are read from the player
 * object, offline ones from the database; stats exist only while online (they are computed
 * at login).
 *
 *   .partybot companion <guid> char|rep|skill     companions you recruited, your account's characters
 *   (his quests: CompanionQuestLog.cpp)
 *
 * Replies (system messages, ';' separated):
 *   PBCMV;<guid>;BEGIN;<page>;<online 0/1>
 *   char   PBCMV;<guid>;I;<slot 0-18>;<item>;<enchant>;<random property>
 *          PBCMV;<guid>;S;<key>=<value>;...   several lines, merged by the addon (online only):
 *                 base     st1-5 sp1-5 sn1-5 (stat, its buffs, its debuffs), arm armP armN, r1-6 (holy..arcane)
 *                 melee    mmin mmax omin omax mspd ospd (ms) dpos dneg dpct ap apP apN mcrit mhit mskill
 *                 ranged   rw (0 none, 1 weapon, 2 wand) rmin rmax rspd rap rapP rapN rcrit rhit rskill
 *                 spell    heal shit, per school 1-6: sd (bonus damage) sc (crit %) sh (hit %), talents included
 *                 regen    mana (0/1) mp5 (gear and effects) mpc (casting) mpn (not casting), per 5 seconds
 *                 defense  def defB (skill, its bonus) dodge parry block bval
 *   rep    PBCMV;<guid>;F;<faction>;<parent faction>;<reputation incl. base>;<at war 0/1>
 *   skill  PBCMV;<guid>;K;<skill>;<category>;<value>;<max>;<bonus>;<name (server DBC)>
 *   PBCMV;<guid>;END;<page>
 *   PBERR;;notcompanion;<guid>   PBERR;;usage
 */

#include "Chat.h"
#include "Player.h"
#include "Item.h"
#include "ReputationMgr.h"
#include "ObjectMgr.h"
#include "ObjectAccessor.h"
#include "DBCStores.h"
#include "SpellMgr.h"
#include "WorldSession.h"
#include "Database/DatabaseEnv.h"
#include "Companions/CompanionMgr.h"
#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    using Lines = std::vector<std::string>;

    std::string Clean(std::string text)
    {
        for (char& c : text)
            if (c == ';' || c == '\n')
                c = ',';
        return text;
    }

    std::string Head(uint32 guid, char const* kind)
    {
        return "PBCMV;" + std::to_string(guid) + ";" + kind + ";";
    }

    // ---- char: gear and stats ----

    void GearOnline(Player const* bot, uint32 guid, Lines& out)
    {
        for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
        {
            Item const* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
            if (!item)
                continue;
            std::ostringstream ss;
            ss << Head(guid, "I") << uint32(slot) << ";" << item->GetEntry() << ";"
               << item->GetEnchantmentId(PERM_ENCHANTMENT_SLOT) << ";" << item->GetItemRandomPropertyId();
            out.push_back(ss.str());
        }
    }

    void GearOffline(uint32 guid, Lines& out)
    {
        std::unique_ptr<QueryResult> result(CharacterDatabase.PQuery(
            "SELECT ci.`slot`, ii.`item_id`, ii.`enchantments`, ii.`random_property_id` FROM `character_inventory` ci"
            " JOIN `item_instance` ii ON ii.`guid` = ci.`item_guid`"
            " WHERE ci.`guid` = %u AND ci.`bag` = 0 AND ci.`slot` < %u", guid, uint32(EQUIPMENT_SLOT_END)));
        if (!result)
            return;
        do
        {
            Field* f = result->Fetch();
            // enchantments: "id duration charges" per slot, the permanent one first
            uint32 enchant = 0;
            std::istringstream(f[2].GetCppString()) >> enchant;
            std::ostringstream ss;
            ss << Head(guid, "I") << f[0].GetUInt32() << ";" << f[1].GetUInt32() << ";" << enchant << ";" << f[3].GetInt32();
            out.push_back(ss.str());
        } while (result->NextRow());
    }

    // One "S" line of key=value pairs; the addon merges every S line into one table.
    class StatLine
    {
    public:
        StatLine(uint32 guid) { m_ss << Head(guid, "S"); m_ss.setf(std::ios::fixed); m_ss.precision(2); }
        template <class T> StatLine& operator()(char const* key, T value) { m_ss << (m_first ? "" : ";") << key << "=" << value; m_first = false; return *this; }
        std::string str() const { return m_ss.str(); }
    private:
        std::ostringstream m_ss;
        bool m_first = true;
    };

    // The best talent/aura spell mod (critical chance, hit) the bot has on any of its own spells of
    // each school: the client works these out per talent (CharacterStatsClassic), the server knows them.
    void BestSpellModBySchool(Player* bot, SpellModOp op, bool harmfulOnly, float (&best)[MAX_SPELL_SCHOOL])
    {
        for (float& v : best)
            v = 0.0f;
        for (auto const& known : bot->GetSpellMap())
        {
            if (known.second.state == PLAYERSPELL_REMOVED || known.second.disabled || !known.second.active)
                continue;
            SpellEntry const* spell = sSpellMgr.GetSpellEntry(known.first);
            if (!spell || spell->IsPassiveSpell() || spell->School >= MAX_SPELL_SCHOOL || (harmfulOnly && spell->IsPositiveSpell()))
                continue;
            float bonus = 0.0f;
            bot->ApplySpellMod(spell->Id, op, bonus, nullptr, false);
            best[spell->School] = std::max(best[spell->School], bonus);
        }
    }

    // The fields CharacterStatsClassic reads from the player's own API (Sheet/CharacterStats.lua),
    // which the client has for nobody else.
    void StatLines(Player* bot, uint32 guid, Lines& out)
    {
        StatLine base(guid);
        for (uint8 stat = STAT_STRENGTH; stat <= STAT_SPIRIT; ++stat)
        {
            std::string const n = std::to_string(stat + 1);
            base(("st" + n).c_str(), int32(bot->GetStat(Stats(stat))))
                (("sp" + n).c_str(), int32(bot->GetPosStat(Stats(stat))))
                (("sn" + n).c_str(), int32(bot->GetNegStat(Stats(stat))));
        }
        base("arm", bot->GetArmor())
            ("armP", int32(bot->GetResistanceBuffMods(SPELL_SCHOOL_NORMAL, true)))
            ("armN", int32(bot->GetResistanceBuffMods(SPELL_SCHOOL_NORMAL, false)));
        for (uint8 school = SPELL_SCHOOL_HOLY; school < MAX_SPELL_SCHOOL; ++school)
            base(("r" + std::to_string(school)).c_str(), bot->GetResistance(SpellSchools(school)));
        out.push_back(base.str());

        StatLine melee(guid);
        bool const offhand = bot->HaveOffhandWeapon();
        melee("mmin", bot->GetFloatValue(UNIT_FIELD_MINDAMAGE))("mmax", bot->GetFloatValue(UNIT_FIELD_MAXDAMAGE))
            ("omin", offhand ? bot->GetFloatValue(UNIT_FIELD_MINOFFHANDDAMAGE) : 0.0f)
            ("omax", offhand ? bot->GetFloatValue(UNIT_FIELD_MAXOFFHANDDAMAGE) : 0.0f)
            ("mspd", bot->GetAttackTime(BASE_ATTACK))("ospd", offhand ? bot->GetAttackTime(OFF_ATTACK) : 0)
            ("dpos", bot->GetInt32Value(PLAYER_FIELD_MOD_DAMAGE_DONE_POS))("dneg", bot->GetInt32Value(PLAYER_FIELD_MOD_DAMAGE_DONE_NEG))
            ("dpct", bot->GetFloatValue(PLAYER_FIELD_MOD_DAMAGE_DONE_PCT))
            ("ap", bot->GetInt32Value(UNIT_FIELD_ATTACK_POWER))
            ("apP", bot->GetInt16Value(UNIT_FIELD_ATTACK_POWER_MODS, 0))("apN", bot->GetInt16Value(UNIT_FIELD_ATTACK_POWER_MODS, 1))
            ("mcrit", bot->GetFloatValue(PLAYER_CRIT_PERCENTAGE))
            ("mhit", bot->GetWeaponBasedAuraModifier(BASE_ATTACK, SPELL_AURA_MOD_HIT_CHANCE))
            ("mskill", bot->GetWeaponSkillValue(BASE_ATTACK));
        out.push_back(melee.str());

        // rw: 0 no ranged weapon, 1 bow/gun/crossbow/thrown, 2 wand (no attack power)
        Item const* rangedItem = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_RANGED);
        ItemPrototype const* rangedProto = rangedItem ? rangedItem->GetProto() : nullptr;
        int const rw = !rangedProto || rangedProto->Class != ITEM_CLASS_WEAPON ? 0
            : rangedProto->SubClass == ITEM_SUBCLASS_WEAPON_WAND ? 2 : 1;
        StatLine ranged(guid);
        ranged("rw", rw)("rmin", bot->GetFloatValue(UNIT_FIELD_MINRANGEDDAMAGE))("rmax", bot->GetFloatValue(UNIT_FIELD_MAXRANGEDDAMAGE))
            ("rspd", bot->GetAttackTime(RANGED_ATTACK))
            ("rap", bot->GetInt32Value(UNIT_FIELD_RANGED_ATTACK_POWER))
            ("rapP", bot->GetInt16Value(UNIT_FIELD_RANGED_ATTACK_POWER_MODS, 0))("rapN", bot->GetInt16Value(UNIT_FIELD_RANGED_ATTACK_POWER_MODS, 1))
            ("rcrit", bot->GetFloatValue(PLAYER_RANGED_CRIT_PERCENTAGE))
            ("rhit", bot->GetWeaponBasedAuraModifier(RANGED_ATTACK, SPELL_AURA_MOD_HIT_CHANCE))
            ("rskill", bot->GetWeaponSkillValue(RANGED_ATTACK));
        out.push_back(ranged.str());

        // per school 1..6 (holy..arcane): bonus damage, crit and hit including the best talent spell mod
        float critMod[MAX_SPELL_SCHOOL], hitMod[MAX_SPELL_SCHOOL];
        BestSpellModBySchool(bot, SPELLMOD_CRITICAL_CHANCE, false, critMod);
        BestSpellModBySchool(bot, SPELLMOD_RESIST_MISS_CHANCE, true, hitMod);
        StatLine spell(guid);
        spell("heal", int32(bot->SpellBaseHealingBonusDone(SPELL_SCHOOL_MASK_ALL)))("shit", bot->m_modSpellHitChance);
        for (uint8 school = SPELL_SCHOOL_HOLY; school < MAX_SPELL_SCHOOL; ++school)
        {
            std::string const n = std::to_string(school);
            spell(("sd" + n).c_str(), bot->SpellBaseDamageBonusDone(GetSchoolMask(school)))
                (("sc" + n).c_str(), bot->GetSpellCritPercent(SpellSchools(school)) + critMod[school])
                (("sh" + n).c_str(), bot->m_modSpellHitChance + hitMod[school]);
        }
        out.push_back(spell.str());

        // mana per 5 seconds: from gear and effects, while casting, while not casting
        StatLine regen(guid);
        bool const mana = bot->GetMaxPower(POWER_MANA) > 0;
        regen("mana", mana ? 1 : 0)
            ("mp5", bot->GetTotalAuraModifierByMiscValue(SPELL_AURA_MOD_POWER_REGEN, POWER_MANA))
            ("mpc", bot->GetManaRegenPerSec(true) * 5.0f)("mpn", bot->GetManaRegenPerSec(false) * 5.0f);
        out.push_back(regen.str());

        StatLine defense(guid);
        uint16 const defPure = bot->GetSkillValuePure(SKILL_DEFENSE);
        defense("def", defPure)("defB", int32(bot->GetSkillValue(SKILL_DEFENSE)) - int32(defPure))
            ("dodge", bot->GetFloatValue(PLAYER_DODGE_PERCENTAGE))("parry", bot->GetFloatValue(PLAYER_PARRY_PERCENTAGE))
            ("block", bot->GetFloatValue(PLAYER_BLOCK_PERCENTAGE))("bval", bot->GetShieldBlockValue());
        out.push_back(defense.str());
    }

    // ---- rep ----

    bool ShownInList(uint32 flags)
    {
        return (flags & FACTION_FLAG_VISIBLE) && !(flags & (FACTION_FLAG_HIDDEN | FACTION_FLAG_INVISIBLE_FORCED));
    }

    void Faction(uint32 guid, FactionEntry const* entry, int32 reputation, uint32 flags, Lines& out)
    {
        std::ostringstream ss;
        ss << Head(guid, "F") << entry->ID << ";" << entry->team << ";" << reputation << ";"
           << ((flags & FACTION_FLAG_AT_WAR) ? 1 : 0);
        out.push_back(ss.str());
    }

    void RepOnline(Player const* bot, uint32 guid, Lines& out)
    {
        ReputationMgr const& mgr = bot->GetReputationMgr();
        for (auto const& itr : mgr.GetStateList())
        {
            FactionState const& state = itr.second;
            FactionEntry const* entry = sObjectMgr.GetFactionEntry(state.ID);
            if (entry && ShownInList(state.Flags))
                Faction(guid, entry, mgr.GetReputation(entry), state.Flags, out);
        }
    }

    void RepOffline(uint32 guid, uint8 race, uint8 cls, Lines& out)
    {
        std::unique_ptr<QueryResult> result(CharacterDatabase.PQuery(
            "SELECT `faction`, `standing`, `flags` FROM `character_reputation` WHERE `guid` = %u", guid));
        if (!result)
            return;
        uint32 const raceMask = race ? 1u << (race - 1) : 0;
        uint32 const classMask = cls ? 1u << (cls - 1) : 0;
        do
        {
            Field* f = result->Fetch();
            FactionEntry const* entry = sObjectMgr.GetFactionEntry(f[0].GetUInt32());
            uint32 const flags = f[2].GetUInt32();
            if (!entry || !ShownInList(flags))
                continue;
            // the table holds what was earned; the base comes from the race and class (ReputationMgr::GetBaseReputation)
            int const index = entry->GetIndexFitTo(raceMask, classMask);
            int32 const base = index >= 0 ? entry->BaseRepValue[index] : 0;
            Faction(guid, entry, base + f[1].GetInt32(), flags, out);
        } while (result->NextRow());
    }

    // ---- skill ----

    void Skill(uint32 guid, uint32 skillId, uint32 value, uint32 max, int32 bonus, LocaleConstant locale, Lines& out)
    {
        SkillLineEntry const* line = sSkillLineStore.LookupEntry(skillId);
        if (!line)
            return;
        char const* name = line->name[locale] && *line->name[locale] ? line->name[locale] : line->name[0];
        std::ostringstream ss;
        ss << Head(guid, "K") << skillId << ";" << line->categoryId << ";" << value << ";" << max << ";" << bonus << ";"
           << Clean(name ? name : "");
        out.push_back(ss.str());
    }

    void SkillsOnline(Player const* bot, uint32 guid, LocaleConstant locale, Lines& out)
    {
        for (uint32 id = 0; id < sSkillLineStore.GetNumRows(); ++id)
            if (sSkillLineStore.LookupEntry(id) && bot->HasSkill(uint16(id)))
                Skill(guid, id, bot->GetSkillValuePure(uint16(id)), bot->GetSkillMaxPure(uint16(id)),
                    int32(bot->GetSkillValue(uint16(id))) - int32(bot->GetSkillValuePure(uint16(id))), locale, out);
    }

    void SkillsOffline(uint32 guid, LocaleConstant locale, Lines& out)
    {
        std::unique_ptr<QueryResult> result(CharacterDatabase.PQuery(
            "SELECT `skill`, `value`, `max` FROM `character_skills` WHERE `guid` = %u", guid));
        if (!result)
            return;
        do
        {
            Field* f = result->Fetch();
            Skill(guid, f[0].GetUInt32(), f[1].GetUInt32(), f[2].GetUInt32(), 0, locale, out);
        } while (result->NextRow());
    }
}

bool ChatHandler::HandlePartyBotCompanionCommand(char* args)
{
    Player* player = GetSession() ? GetSession()->GetPlayer() : nullptr;
    uint32 guid = 0;
    char* pageArg = nullptr;
    if (!player || !ExtractUInt32(&args, guid) || !(pageArg = ExtractLiteralArg(&args)))
    {
        SendSysMessage("PBERR;;usage;.partybot companion <guid> char|rep|skill");
        SetSentErrorMessage(true);
        return false;
    }

    // (yours, or a character of your account: companion account doc)
    if (!sCompanionMgr.MayManage(player, guid))
    {
        PSendSysMessage("PBERR;;notcompanion;%u", guid);
        SetSentErrorMessage(true);
        return false;
    }

    std::string const page = pageArg;
    Player* bot = sObjectAccessor.FindPlayer(ObjectGuid(HIGHGUID_PLAYER, guid));
    if (bot && !bot->IsInWorld())
        bot = nullptr;
    LocaleConstant const locale = GetSessionDbcLocale();

    Lines lines;
    if (page == "char")
    {
        if (bot)
        {
            GearOnline(bot, guid, lines);
            StatLines(bot, guid, lines);
        }
        else
            GearOffline(guid, lines);
    }
    else if (page == "rep")
    {
        if (bot)
            RepOnline(bot, guid, lines);
        else if (std::unique_ptr<QueryResult> who{ CharacterDatabase.PQuery("SELECT `race`, `class` FROM `characters` WHERE `guid` = %u", guid) })
            RepOffline(guid, who->Fetch()[0].GetUInt8(), who->Fetch()[1].GetUInt8(), lines);
    }
    else if (page == "skill")
    {
        if (bot)
            SkillsOnline(bot, guid, locale, lines);
        else
            SkillsOffline(guid, locale, lines);
    }
    else
    {
        SendSysMessage("PBERR;;usage;.partybot companion <guid> char|rep|skill");
        SetSentErrorMessage(true);
        return false;
    }

    PSendSysMessage("PBCMV;%u;BEGIN;%s;%u", guid, page.c_str(), bot ? 1 : 0);
    for (std::string const& line : lines)
        SendSysMessage(line.c_str());
    PSendSysMessage("PBCMV;%u;END;%s", guid, page.c_str());
    return true;
}
