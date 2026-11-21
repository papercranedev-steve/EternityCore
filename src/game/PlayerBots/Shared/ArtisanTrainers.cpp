#include "Shared/ArtisanTrainers.h"
#include "Shared/Professions.h"
#include "Shared/QuestMirror.h"
#include "Player.h"
#include "ObjectMgr.h"
#include "SpellMgr.h"
#include "SpellEntry.h"
#include "DBCStores.h"
#include "World.h"
#include <algorithm>
#include <map>
#include <mutex>

namespace BotShared
{
namespace ArtisanTrainers
{
    namespace
    {
        namespace P = Professions;

        P::Rank const* ArtisanRank(uint32 skill)
        {
            for (P::Rank const& rank : P::RanksOf(skill))
                if (rank.maxValue >= P::ARTISAN_MAX)
                    return &rank;
            return nullptr;
        }

        std::mutex s_lock;
        std::map<uint32, std::vector<Spawn>> s_bySkill;
        bool s_built = false;

        // With s_lock held: every spawn of a trainer teaching an Artisan rank, by skill.
        void Build()
        {
            if (s_built)
                return;
            s_built = true;
            std::map<uint32, uint32> rankSkill;     // rank spell -> skill
            for (uint32 skill : P::Primaries())
                if (P::Rank const* rank = ArtisanRank(skill))
                    rankSkill[rank->spellId] = skill;

            std::map<uint32, std::vector<uint32>> skillsOfEntry;   // creature entry -> skills (cached per entry)
            auto skillsOf = [&](uint32 entry) -> std::vector<uint32> const&
            {
                auto itr = skillsOfEntry.find(entry);
                if (itr != skillsOfEntry.end())
                    return itr->second;
                std::vector<uint32>& skills = skillsOfEntry[entry];
                for (auto const& r : rankSkill)
                    if (P::Teaches(entry, r.first))
                        skills.push_back(r.second);
                return skills;
            };
            auto collect = [&](CreatureDataPair const& pair)
            {
                CreatureData const& d = pair.second;
                if ((d.spawn_flags & (SPAWN_FLAG_DISABLED | SPAWN_FLAG_DEAD)) || !d.creature_id[0])
                    return false;
                CreatureInfo const* info = sObjectMgr.GetCreatureTemplate(d.creature_id[0]);
                if (!info || !(info->npc_flags & UNIT_NPC_FLAG_TRAINER) || info->trainer_type != TRAINER_TYPE_TRADESKILLS)
                    return false;
                for (uint32 skill : skillsOf(d.creature_id[0]))
                {
                    Spawn s;
                    s.dbGuid = pair.first;
                    s.entry = d.creature_id[0];
                    s.mapId = d.position.mapId;
                    s.x = d.position.x;
                    s.y = d.position.y;
                    s.z = d.position.z;
                    s_bySkill[skill].push_back(s);
                }
                return false;
            };
            sObjectMgr.DoCreatureData(collect);
        }

        bool Hostile(Player const* player, uint32 entry)
        {
            CreatureInfo const* info = sObjectMgr.GetCreatureTemplate(entry);
            FactionTemplateEntry const* theirs = info ? sObjectMgr.GetFactionTemplateEntry(info->faction) : nullptr;
            FactionTemplateEntry const* ours = player->GetFactionTemplateEntry();
            return !theirs || !ours || theirs->IsHostileTo(*ours);
        }
    }

    uint32 RankSpell(uint32 skill)
    {
        P::Rank const* rank = ArtisanRank(skill);
        return rank ? rank->spellId : 0;
    }

    uint32 RankCost(uint32 skill)
    {
        P::Rank const* rank = ArtisanRank(skill);
        return rank ? rank->cost : 0;
    }

    uint32 RankReqSkill(uint32 skill)
    {
        P::Rank const* rank = ArtisanRank(skill);
        return rank ? rank->reqSkillValue : 0;
    }

    uint32 RankReqLevel(uint32 skill)
    {
        P::Rank const* rank = ArtisanRank(skill);
        if (!rank)
            return 0;
        if (rank->reqLevel)
            return rank->reqLevel;
        SpellEntry const* spell = sSpellMgr.GetSpellEntry(rank->spellId);
        return spell ? spell->spellLevel : 0;
    }

    std::vector<Spawn> For(Player const* player, uint32 skill, uint32 mapId, float x, float y)
    {
        std::vector<Spawn> all;
        {
            std::lock_guard<std::mutex> guard(s_lock);
            Build();
            auto itr = s_bySkill.find(skill);
            if (itr != s_bySkill.end())
                all = itr->second;
        }
        all.erase(std::remove_if(all.begin(), all.end(), [player](Spawn const& s) { return Hostile(player, s.entry); }), all.end());
        std::stable_sort(all.begin(), all.end(), [mapId, x, y](Spawn const& a, Spawn const& b)
        {
            bool const aHere = a.mapId == mapId;
            bool const bHere = b.mapId == mapId;
            if (aHere != bHere)
                return aHere;
            float const da = (a.x - x) * (a.x - x) + (a.y - y) * (a.y - y);
            float const db = (b.x - x) * (b.x - x) + (b.y - y) * (b.y - y);
            return aHere && da < db;
        });
        return all;
    }

    std::string NameOf(uint32 entry, int localeIdx)
    {
        CreatureInfo const* info = sObjectMgr.GetCreatureTemplate(entry);
        std::string name = info ? std::string(info->name) : std::string();
        if (CreatureLocale const* locale = sObjectMgr.GetCreatureLocale(entry))
            if (localeIdx >= 0 && localeIdx < int(locale->Name.size()) && !locale->Name[localeIdx].empty())
                name = locale->Name[localeIdx];
        return name;
    }

    uint32 QuestOf(uint32 skill)
    {
        return QuestMirror::TripId(skill);
    }

    uint32 BuildQuests()
    {
        uint32 made = 0;
        for (uint32 skill : P::Primaries())
        {
            if (!ArtisanRank(skill))
                continue;
            SkillLineEntry const* line = sSkillLineStore.LookupEntry(skill);
            auto skillName = [line](int idx) -> std::string
            {
                if (!line)
                    return std::string();
                // A loaded database locale need not have a dbc (ruRU = 8 is past name[]): as a session does.
                LocaleConstant const locale = sWorld.GetAvailableDbcLocale(idx < 0 ? LOCALE_enUS : sObjectMgr.GetLocaleForIndex(idx));
                char const* name = line->name[locale] && *line->name[locale] ? line->name[locale] : line->name[0];
                return name ? name : "";
            };
            auto text = [&skillName](int idx) -> QuestMirror::TripText
            {
                LocaleConstant const locale = idx < 0 ? LOCALE_enUS : sObjectMgr.GetLocaleForIndex(idx);
                std::string const name = skillName(idx);
                QuestMirror::TripText t;
                if (locale == LOCALE_zhCN || locale == LOCALE_zhTW)
                {
                    t.title = "大师级" + name;
                    t.objective = "带队友去找大师级" + name + "训练师";
                    t.details = "队友的" + name + "已经可以学大师级了，可大师级训练师不在主城里。$B$B带他去找训练师"
                        "（同一张大地图上会有红旗），到了附近他自己会过去，学费他自己出。";
                }
                else
                {
                    t.title = "Artisan " + name;
                    t.objective = "Bring your companion to an Artisan " + name + " trainer";
                    t.details = "Your companion is ready for Artisan " + name + ", but no capital trainer teaches it.$B$B"
                        "Take them to the trainer (a red flag on your map when you are on the same continent); "
                        "near it they walk over and pay the fee themselves.";
                }
                return t;
            };
            if (QuestMirror::Builder::BuildTrip(QuestOf(skill), 35, text))
                ++made;
        }
        return made;
    }
}
}
