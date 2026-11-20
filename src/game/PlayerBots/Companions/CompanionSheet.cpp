/*
 * Companions: the character sheet of one companion, a page at a time (the addon's companion
 * window, addons/PartyBot/CompanionSheet.lua). Online companions are read from the player
 * object, offline ones from the database; stats exist only while online (they are computed
 * at login).
 *
 *   .partybot companion <guid> char|rep|skill|honor     only companions you recruited
 *
 * Replies (system messages, ';' separated):
 *   PBCMV;<guid>;BEGIN;<page>;<online 0/1>
 *   char   PBCMV;<guid>;I;<slot 0-18>;<item>;<enchant>;<random property>
 *          PBCMV;<guid>;S;<str>;<agi>;<sta>;<int>;<spi>;<armor>;<holy>;<fire>;<nature>;<frost>;<shadow>;<arcane>;
 *                 <melee skill>;<melee ap>;<melee min>;<melee max>;<melee speed ms>;
 *                 <ranged skill>;<ranged ap>;<ranged min>;<ranged max>;<ranged speed ms>     (online only)
 *   rep    PBCMV;<guid>;F;<faction>;<parent faction>;<reputation incl. base>;<at war 0/1>
 *   skill  PBCMV;<guid>;K;<skill>;<category>;<value>;<max>;<bonus>;<name (server DBC)>
 *   honor  PBCMV;<guid>;H;<rank 0-18>;<highest rank 0-18>;<honorable kills>;<dishonorable kills>;
 *                 <last week kills>;<last week contribution>;<last week standing>
 *   PBCMV;<guid>;END;<page>
 *   PBERR;;notcompanion;<guid>   PBERR;;usage
 */

#include "Chat.h"
#include "Player.h"
#include "Item.h"
#include "HonorMgr.h"
#include "ReputationMgr.h"
#include "ObjectMgr.h"
#include "ObjectAccessor.h"
#include "DBCStores.h"
#include "WorldSession.h"
#include "Database/DatabaseEnv.h"
#include "Companions/CompanionMgr.h"
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

    void StatLine(Player* bot, uint32 guid, Lines& out)
    {
        std::ostringstream ss;
        ss << Head(guid, "S");
        for (uint8 stat = STAT_STRENGTH; stat <= STAT_SPIRIT; ++stat)
            ss << int32(bot->GetStat(Stats(stat))) << ";";
        ss << bot->GetArmor() << ";";
        for (uint8 school = SPELL_SCHOOL_HOLY; school <= SPELL_SCHOOL_ARCANE; ++school)
            ss << bot->GetResistance(SpellSchools(school)) << ";";
        ss << bot->GetWeaponSkillValue(BASE_ATTACK) << ";" << int32(bot->GetTotalAttackPowerValue(BASE_ATTACK)) << ";"
           << int32(bot->GetFloatValue(UNIT_FIELD_MINDAMAGE)) << ";" << int32(bot->GetFloatValue(UNIT_FIELD_MAXDAMAGE)) << ";"
           << bot->GetAttackTime(BASE_ATTACK) << ";";
        bool const ranged = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_RANGED) != nullptr;
        ss << (ranged ? bot->GetWeaponSkillValue(RANGED_ATTACK) : 0) << ";" << int32(bot->GetTotalAttackPowerValue(RANGED_ATTACK)) << ";"
           << (ranged ? int32(bot->GetFloatValue(UNIT_FIELD_MINRANGEDDAMAGE)) : 0) << ";"
           << (ranged ? int32(bot->GetFloatValue(UNIT_FIELD_MAXRANGEDDAMAGE)) : 0) << ";"
           << (ranged ? bot->GetAttackTime(RANGED_ATTACK) : 0);
        out.push_back(ss.str());
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

    // ---- honor ----

    void Honor(uint32 guid, uint32 rank, uint32 highest, uint32 hk, uint32 dk, uint32 lastWeekHk, uint32 lastWeekCp,
        uint32 standing, Lines& out)
    {
        std::ostringstream ss;
        ss << Head(guid, "H") << rank << ";" << highest << ";" << hk << ";" << dk << ";" << lastWeekHk << ";"
           << lastWeekCp << ";" << standing;
        out.push_back(ss.str());
    }

    void HonorOnline(Player const* bot, uint32 guid, Lines& out)
    {
        HonorMgr const& mgr = bot->GetHonorMgr();
        Honor(guid, mgr.GetCurrentHonorRank(), mgr.GetHighestRank().rank, mgr.GetTotalHK(), mgr.GetTotalDK(),
            mgr.GetLastWeekHK(), uint32(mgr.GetLastWeekCP()), mgr.GetStanding(), out);
    }

    void HonorOffline(uint32 guid, Lines& out)
    {
        std::unique_ptr<QueryResult> result(CharacterDatabase.PQuery(
            "SELECT `honor_rank_points`, `honor_highest_rank`, `honor_stored_hk`, `honor_stored_dk`, `honor_last_week_hk`,"
            " `honor_last_week_cp`, `honor_standing` FROM `characters` WHERE `guid` = %u", guid));
        if (!result)
            return;
        Field* f = result->Fetch();
        uint32 const hk = f[2].GetUInt32();
        HonorRankInfo const rank = HonorMgr::CalculateRank(f[0].GetFloat(), hk);
        Honor(guid, rank.rank, f[1].GetUInt32(), hk, f[3].GetUInt32(), f[4].GetUInt32(), uint32(f[5].GetFloat()),
            f[6].GetUInt32(), out);
    }
}

bool ChatHandler::HandlePartyBotCompanionCommand(char* args)
{
    Player* player = GetSession() ? GetSession()->GetPlayer() : nullptr;
    uint32 guid = 0;
    char* pageArg = nullptr;
    if (!player || !ExtractUInt32(&args, guid) || !(pageArg = ExtractLiteralArg(&args)))
    {
        SendSysMessage("PBERR;;usage;.partybot companion <guid> char|rep|skill|honor");
        SetSentErrorMessage(true);
        return false;
    }

    Companions::CompanionInfo const* info = sCompanionMgr.Find(guid);
    if (!info || info->recruiterGuid != player->GetGUIDLow())
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
            StatLine(bot, guid, lines);
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
    else if (page == "honor")
    {
        if (bot)
            HonorOnline(bot, guid, lines);
        else
            HonorOffline(guid, lines);
    }
    else
    {
        SendSysMessage("PBERR;;usage;.partybot companion <guid> char|rep|skill|honor");
        SetSentErrorMessage(true);
        return false;
    }

    PSendSysMessage("PBCMV;%u;BEGIN;%s;%u", guid, page.c_str(), bot ? 1 : 0);
    for (std::string const& line : lines)
        SendSysMessage(line.c_str());
    PSendSysMessage("PBCMV;%u;END;%s", guid, page.c_str());
    return true;
}
