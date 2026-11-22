/*
 * Companions: one companion's quest log (the addon's companion quest log window,
 * addons/PartyBot/CompanionQuestLog.lua), built like the player's own: the quests in log
 * order, whether each is done or failed, and the progress of every objective. Online
 * companions are read from the player object, offline ones from the database.
 *
 *   .partybot questlog <guid>      companions you recruited, your account's characters
 *
 * Replies (system messages, ';' separated, free text last):
 *   PBQLOG;<guid>;BEGIN;<online 0/1>
 *   PBQLOG;<guid>;Q;<quest>;<level, 0 = his level>;<state 0 in progress / 1 complete / -1 failed>;
 *          <type (QuestInfo)>;<zone or -sort>;<header>;<title>
 *   PBQLOG;<guid>;O;<quest>;<m creature / g object / i item>;<have>;<need>;<name>
 *   PBQLOG;<guid>;T;<quest>;<objectives text, a piece>          (pieces in order, joined by the addon)
 *   PBQLOG;<guid>;D;<quest>;<description, a piece>
 *   PBQLOG;<guid>;END
 *   PBERR;;notcompanion;<guid>   PBERR;;usage
 *
 * Texts keep the quest tokens ($N, $C, $R, $G, $B): the addon fills them in for him.
 * Header: the zone's (English) name, the class or profession of a quest sort, else empty;
 * the addon prefers the client's own zone names.
 */

#include "Chat.h"
#include "Player.h"
#include "ObjectMgr.h"
#include "ObjectAccessor.h"
#include "DBCStores.h"
#include "Map.h"
#include "QuestDef.h"
#include "Database/DatabaseEnv.h"
#include "Companions/CompanionMgr.h"
#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    using Lines = std::vector<std::string>;

    // a system message stays well under the chat limit
    constexpr size_t PIECE_BYTES = 200;

    struct Entry
    {
        uint32 questId;
        QuestStatusData data;
    };

    std::string Clean(std::string text)
    {
        std::string out;
        out.reserve(text.size());
        for (char c : text)
        {
            if (c == '\r')
                continue;
            if (c == '\n')
                out += "$B";        // the addon turns it back into a line break
            else if (c == ';')
                out += ',';
            else
                out += c;
        }
        return out;
    }

    std::string Head(uint32 guid, char const* kind, uint32 questId)
    {
        return "PBQLOG;" + std::to_string(guid) + ";" + kind + ";" + std::to_string(questId) + ";";
    }

    template <class T>
    std::string Localized(T const* locale, std::vector<std::string> T::* field, int idx, std::string const& fallback)
    {
        if (locale && idx >= 0 && idx < int((locale->*field).size()) && !(locale->*field)[idx].empty())
            return (locale->*field)[idx];
        return fallback;
    }

    std::string ObjectiveText(Quest const* quest, QuestLocale const* locale, int idx, uint8 i)
    {
        if (locale && idx >= 0 && i < locale->ObjectiveText.size() && idx < int(locale->ObjectiveText[i].size())
            && !locale->ObjectiveText[i][idx].empty())
            return locale->ObjectiveText[i][idx];
        return quest->ObjectiveText[i];
    }

    std::string CreatureName(uint32 id, int idx)
    {
        CreatureInfo const* info = sObjectMgr.GetCreatureTemplate(id);
        if (!info)
            return std::to_string(id);
        return Localized(sObjectMgr.GetCreatureLocale(id), &CreatureLocale::Name, idx, info->name);
    }

    std::string ObjectName(uint32 id, int idx)
    {
        GameObjectInfo const* info = sObjectMgr.GetGameObjectTemplate(id);
        if (!info)
            return std::to_string(id);
        return Localized(sObjectMgr.GetGameObjectLocale(id), &GameObjectLocale::Name, idx, info->name);
    }

    std::string ItemName(uint32 id, int idx)
    {
        ItemPrototype const* proto = sObjectMgr.GetItemPrototype(id);
        if (!proto)
            return std::to_string(id);
        return Localized(sObjectMgr.GetItemLocale(id), &ItemLocale::Name, idx, proto->Name1 ? proto->Name1 : "");
    }

    std::string Header(int32 zoneOrSort, LocaleConstant dbc)
    {
        if (zoneOrSort > 0)
        {
            AreaEntry const* area = AreaEntry::GetById(uint32(zoneOrSort));
            return area && area->Name ? area->Name : "";
        }
        if (zoneOrSort < 0)
        {
            if (uint8 const cls = ClassByQuestSort(-zoneOrSort))
                if (ChrClassesEntry const* entry = sChrClassesStore.LookupEntry(cls))
                    return entry->name[dbc] && *entry->name[dbc] ? entry->name[dbc] : (entry->name[0] ? entry->name[0] : "");
            if (uint32 const skill = SkillByQuestSort(-zoneOrSort))
                if (SkillLineEntry const* line = sSkillLineStore.LookupEntry(skill))
                    return line->name[dbc] && *line->name[dbc] ? line->name[dbc] : (line->name[0] ? line->name[0] : "");
        }
        return "";
    }

    // long text in pieces, never cutting a UTF-8 character
    void Pieces(uint32 guid, char const* kind, uint32 questId, std::string const& text, Lines& out)
    {
        size_t pos = 0;
        while (pos < text.size())
        {
            size_t end = std::min(text.size(), pos + PIECE_BYTES);
            while (end < text.size() && end > pos && (uint8(text[end]) & 0xC0) == 0x80)
                --end;
            out.push_back(Head(guid, kind, questId) + text.substr(pos, end - pos));
            pos = end;
        }
    }

    // in the log: taken, not yet handed in (Player::_LoadQuestStatus)
    bool InLog(Quest const* quest, QuestStatusData const& data)
    {
        bool const open = data.m_status == QUEST_STATUS_INCOMPLETE || data.m_status == QUEST_STATUS_COMPLETE
            || data.m_status == QUEST_STATUS_FAILED;
        return open && (!data.m_rewarded || quest->IsRepeatable());
    }

    void QuestLines(uint32 guid, Entry const& e, int idx, LocaleConstant dbc, Lines& out)
    {
        Quest const* quest = sObjectMgr.GetQuestTemplate(e.questId);
        if (!quest)
            return;
        QuestLocale const* locale = sObjectMgr.GetQuestLocale(e.questId);
        QuestStatusData const& d = e.data;

        bool const explorationDone = quest->HasSpecialFlag(QUEST_SPECIAL_FLAG_EXPLORATION_OR_EVENT) && d.m_explored;
        int const state = d.m_status == QUEST_STATUS_FAILED ? -1
            : (d.m_status == QUEST_STATUS_COMPLETE || explorationDone) ? 1 : 0;

        std::ostringstream q;
        q << Head(guid, "Q", e.questId) << quest->GetQuestLevel() << ";" << state << ";" << quest->GetType() << ";"
          << quest->GetZoneOrSort() << ";" << Clean(Header(quest->GetZoneOrSort(), dbc)) << ";"
          << Clean(Localized(locale, &QuestLocale::Title, idx, quest->GetTitle()));
        out.push_back(q.str());

        for (uint8 i = 0; i < QUEST_OBJECTIVES_COUNT; ++i)
        {
            int32 const target = quest->ReqCreatureOrGOId[i];
            uint32 const need = quest->ReqCreatureOrGOCount[i];
            if (!target || !need)
                continue;
            std::string name = ObjectiveText(quest, locale, idx, i);
            bool const creature = target > 0;
            if (name.empty())
                name = creature ? CreatureName(uint32(target), idx) : ObjectName(uint32(-target), idx);
            std::ostringstream o;
            o << Head(guid, "O", e.questId) << (creature && ObjectiveText(quest, locale, idx, i).empty() ? "m" : "g") << ";"
              << std::min(d.m_creatureOrGOcount[i], need) << ";" << need << ";" << Clean(name);
            out.push_back(o.str());
        }
        for (uint8 i = 0; i < QUEST_ITEM_OBJECTIVES_COUNT; ++i)
        {
            uint32 const item = quest->ReqItemId[i];
            uint32 const need = quest->ReqItemCount[i];
            if (!item || !need)
                continue;
            std::ostringstream o;
            o << Head(guid, "O", e.questId) << "i;" << std::min(d.m_itemcount[i], need) << ";" << need << ";"
              << Clean(ItemName(item, idx));
            out.push_back(o.str());
        }

        Pieces(guid, "T", e.questId, Clean(Localized(locale, &QuestLocale::Objectives, idx, quest->GetObjectives())), out);
        Pieces(guid, "D", e.questId, Clean(Localized(locale, &QuestLocale::Details, idx, quest->GetDetails())), out);
    }

    std::vector<Entry> LogOnline(Player* bot)
    {
        std::vector<std::pair<uint16, Entry>> slotted;
        for (auto const& status : bot->GetQuestStatusMap())
        {
            uint16 const slot = bot->FindQuestSlot(status.first);
            if (slot < MAX_QUEST_LOG_SIZE)
                slotted.push_back({ slot, Entry{ status.first, status.second } });
        }
        std::sort(slotted.begin(), slotted.end(),
            [](std::pair<uint16, Entry> const& a, std::pair<uint16, Entry> const& b) { return a.first < b.first; });
        std::vector<Entry> log;
        for (auto const& s : slotted)
            log.push_back(s.second);
        return log;
    }

    // the rows in the order the login reads them, so the slots come out the same
    std::vector<Entry> LogOffline(uint32 guid)
    {
        std::vector<Entry> log;
        std::unique_ptr<QueryResult> result(CharacterDatabase.PQuery(
            "SELECT `quest`, `status`, `rewarded`, `explored`, `mob_count1`, `mob_count2`, `mob_count3`, `mob_count4`,"
            " `item_count1`, `item_count2`, `item_count3`, `item_count4` FROM `character_queststatus` WHERE `guid` = %u", guid));
        if (!result)
            return log;
        do
        {
            Field* f = result->Fetch();
            Entry e{ f[0].GetUInt32(), QuestStatusData() };
            Quest const* quest = sObjectMgr.GetQuestTemplate(e.questId);
            uint32 const status = f[1].GetUInt32();
            if (!quest || status >= MAX_QUEST_STATUS)
                continue;
            e.data.m_status = QuestStatus(status);
            e.data.m_rewarded = f[2].GetUInt8() > 0;
            e.data.m_explored = f[3].GetUInt8() > 0;
            for (uint8 i = 0; i < QUEST_OBJECTIVES_COUNT; ++i)
            {
                e.data.m_creatureOrGOcount[i] = f[4 + i].GetUInt32();
                e.data.m_itemcount[i] = f[8 + i].GetUInt32();
            }
            if (InLog(quest, e.data))
                log.push_back(e);
        } while (result->NextRow() && log.size() < MAX_QUEST_LOG_SIZE);
        return log;
    }
}

bool ChatHandler::HandlePartyBotQuestLogCommand(char* args)
{
    Player* player = GetSession() ? GetSession()->GetPlayer() : nullptr;
    uint32 guid = 0;
    if (!player || !ExtractUInt32(&args, guid))
    {
        SendSysMessage("PBERR;;usage;.partybot questlog <guid>");
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

    Player* bot = sObjectAccessor.FindPlayer(ObjectGuid(HIGHGUID_PLAYER, guid));
    if (bot && !bot->IsInWorld())
        bot = nullptr;

    int const idx = GetSessionDbLocaleIndex();
    LocaleConstant const dbc = GetSessionDbcLocale();
    Lines lines;
    for (Entry const& e : bot ? LogOnline(bot) : LogOffline(guid))
        QuestLines(guid, e, idx, dbc, lines);

    PSendSysMessage("PBQLOG;%u;BEGIN;%u", guid, bot ? 1 : 0);
    for (std::string const& line : lines)
        SendSysMessage(line.c_str());
    PSendSysMessage("PBQLOG;%u;END", guid);
    return true;
}
