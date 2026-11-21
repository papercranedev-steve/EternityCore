#include "Shared/Professions.h"
#include "Player.h"
#include "ObjectMgr.h"
#include "SpellMgr.h"
#include "World.h"
#include <algorithm>
#include <map>
#include <set>

namespace BotShared
{
namespace Professions
{
    namespace
    {
        struct RecipeLesson
        {
            TrainerSpell lesson;    // a copy (a trainer reload does not leave dangling pointers)
            uint32 spellId;         // the recipe it teaches
        };

        struct SkillData
        {
            std::vector<Rank> ranks;
            std::vector<RecipeLesson> recipes;
        };

        // The spells a trainer lesson teaches (its teach spell's learn effects).
        std::vector<uint32> TaughtSpells(TrainerSpell const& lesson)
        {
            std::vector<uint32> taught;
            SpellEntry const* teach = sSpellMgr.GetSpellEntry(lesson.spell);
            if (!teach)
                return taught;
            for (uint32 i = 0; i < MAX_SPELL_EFFECTS; ++i)
                if (teach->Effect[i] == SPELL_EFFECT_LEARN_SPELL && teach->EffectTriggerSpell[i])
                    taught.push_back(teach->EffectTriggerSpell[i]);
            return taught;
        }

        // Every profession trainer's lessons, by skill. Built once.
        std::map<uint32, SkillData> const& Data()
        {
            static std::map<uint32, SkillData> data;
            static bool built = false;
            if (built)
                return data;
            built = true;

            std::set<uint32> seenLessons, seenTemplates;
            auto add = [&](TrainerSpellData const* spells)
            {
                if (!spells)
                    return;
                for (auto const& entry : spells->spellList)
                {
                    TrainerSpell const& lesson = entry.second;
                    if (!seenLessons.insert(lesson.spell).second)
                        continue;
                    for (uint32 spellId : TaughtSpells(lesson))
                    {
                        // A rank sets the skill line's maximum (Mining's trainer spell teaches 2575, which does).
                        if (SpellLearnSkillNode const* node = sSpellMgr.GetSpellLearnSkill(spellId))
                        {
                            if (IsProfession(node->skill))
                                data[node->skill].ranks.push_back({ spellId, node->maxvalue, lesson.reqSkillValue, lesson.reqLevel, lesson.spellCost });
                            continue;
                        }
                        if (lesson.reqSkill && IsProfession(lesson.reqSkill))
                            data[lesson.reqSkill].recipes.push_back({ lesson, spellId });
                    }
                }
            };

            for (auto const& info : sObjectMgr.GetCreatureInfoMap())
            {
                CreatureInfo const* cInfo = info.second.get();
                if (!cInfo || !(cInfo->npc_flags & UNIT_NPC_FLAG_TRAINER) || cInfo->trainer_type != TRAINER_TYPE_TRADESKILLS)
                    continue;
                add(sObjectMgr.GetNpcTrainerSpells(info.first));
                if (cInfo->trainer_id && seenTemplates.insert(cInfo->trainer_id).second)
                    add(sObjectMgr.GetNpcTrainerTemplateSpells(cInfo->trainer_id));
            }

            for (auto& itr : data)
            {
                std::vector<Rank>& ranks = itr.second.ranks;
                std::sort(ranks.begin(), ranks.end(), [](Rank const& a, Rank const& b) { return a.maxValue < b.maxValue; });
                ranks.erase(std::unique(ranks.begin(), ranks.end(), [](Rank const& a, Rank const& b) { return a.spellId == b.spellId; }), ranks.end());
                std::sort(itr.second.recipes.begin(), itr.second.recipes.end(), [](RecipeLesson const& a, RecipeLesson const& b)
                {
                    return a.lesson.reqSkillValue < b.lesson.reqSkillValue;
                });
            }
            return data;
        }

        SkillData const* DataOf(uint32 skill)
        {
            auto itr = Data().find(skill);
            return itr != Data().end() ? &itr->second : nullptr;
        }

        // The profession a spell belongs to, 0 = none.
        uint32 ProfessionOf(uint32 spellId)
        {
            if (SpellLearnSkillNode const* node = sSpellMgr.GetSpellLearnSkill(spellId))
                if (IsProfession(node->skill))
                    return node->skill;
            SkillLineAbilityMapBounds const bounds = sSpellMgr.GetSkillLineAbilityMapBoundsBySpellId(spellId);
            for (auto itr = bounds.first; itr != bounds.second; ++itr)
                if (IsProfession(itr->second->skillId))
                    return itr->second->skillId;
            return 0;
        }
    }

    Kind KindOf(uint32 skill)
    {
        switch (skill)
        {
            case ALCHEMY: case BLACKSMITHING: case ENCHANTING: case ENGINEERING: case LEATHERWORKING: case TAILORING:
                return Kind::Crafting;
            case HERBALISM: case MINING: case SKINNING:
                return Kind::Gathering;
            case FIRST_AID: case COOKING: case FISHING:
                return Kind::Secondary;
            default:
                return Kind::None;
        }
    }

    bool IsProfession(uint32 skill)
    {
        return KindOf(skill) != Kind::None;
    }

    std::vector<uint32> const& Secondaries()
    {
        static std::vector<uint32> const list = { FIRST_AID, COOKING, FISHING };
        return list;
    }

    std::vector<Rank> const& RanksOf(uint32 skill)
    {
        static std::vector<Rank> const none;
        SkillData const* data = DataOf(skill);
        return data ? data->ranks : none;
    }

    uint32 Grant(Player* player, uint32 skill, uint32 value)
    {
        std::vector<Rank> const& ranks = RanksOf(skill);
        if (!player || ranks.empty())
            return 0;

        uint32 maxValue = 0;
        for (Rank const& rank : ranks)
        {
            if (!player->HasSpell(rank.spellId))
                player->LearnSpell(rank.spellId, false);
            maxValue = rank.maxValue;
            if (maxValue >= value)
                break;
        }
        player->SetSkill(uint16(skill), uint16(std::max<uint32>(1, std::min(value, maxValue))), uint16(maxValue));
        return maxValue;
    }

    uint32 LearnRecipes(Player* player, uint32 skill, std::function<bool(uint32 reqSkillValue, uint32 spellId)> const& keep)
    {
        SkillData const* data = DataOf(skill);
        if (!player || !data)
            return 0;

        uint32 learned = 0;
        std::set<uint32> refused;
        // A learned recipe can make another one green (a previous rank): again until nothing is
        // learned, capped in case bad trainer data keeps one green.
        for (uint32 pass = 0; pass < 10; ++pass)
        {
            bool learnedAny = false;
            for (RecipeLesson const& r : data->recipes)
            {
                if (refused.count(r.spellId) || player->HasSpell(r.spellId))
                    continue;
                if (player->GetTrainerSpellState(&r.lesson) != TRAINER_SPELL_GREEN)
                    continue;
                if (!keep(r.lesson.reqSkillValue, r.spellId))
                {
                    refused.insert(r.spellId);
                    continue;
                }
                player->LearnSpell(r.spellId, false);
                ++learned;
                learnedAny = true;
            }
            if (!learnedAny)
                break;
        }
        return learned;
    }

    void ForgetAll(Player* player)
    {
        if (!player)
            return;

        std::vector<uint32> spells;
        for (auto const& itr : player->GetSpellMap())
            if (itr.second.state != PLAYERSPELL_REMOVED && ProfessionOf(itr.first))
                spells.push_back(itr.first);
        for (uint32 spellId : spells)
            player->RemoveSpell(spellId, false, false);

        for (uint32 skill : { ALCHEMY, BLACKSMITHING, ENCHANTING, ENGINEERING, HERBALISM, LEATHERWORKING, MINING,
                              SKINNING, TAILORING, FIRST_AID, COOKING, FISHING })
            if (player->HasSkill(uint16(skill)))
                player->SetSkill(uint16(skill), 0, 0);

        player->SetFreePrimaryProfessions(uint16(sWorld.getConfig(CONFIG_UINT32_MAX_PRIMARY_TRADE_SKILL)));
    }
}
}
