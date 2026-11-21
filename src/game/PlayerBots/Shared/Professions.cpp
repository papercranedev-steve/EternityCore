#include "Shared/Professions.h"
#include "Player.h"
#include "Creature.h"
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
            std::vector<TrainerSpell> rankLessons;  // the lesson of ranks[i]
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
                            {
                                SkillData& d = data[node->skill];
                                if (std::none_of(d.ranks.begin(), d.ranks.end(), [spellId](Rank const& r) { return r.spellId == spellId; }))
                                {
                                    d.ranks.push_back({ spellId, node->maxvalue, lesson.reqSkillValue, lesson.reqLevel, lesson.spellCost });
                                    d.rankLessons.push_back(lesson);
                                }
                            }
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
                SkillData& d = itr.second;
                std::vector<size_t> order(d.ranks.size());
                for (size_t i = 0; i < order.size(); ++i)
                    order[i] = i;
                std::sort(order.begin(), order.end(), [&d](size_t a, size_t b) { return d.ranks[a].maxValue < d.ranks[b].maxValue; });
                std::vector<Rank> ranks;
                std::vector<TrainerSpell> rankLessons;
                for (size_t i : order)
                {
                    ranks.push_back(d.ranks[i]);
                    rankLessons.push_back(d.rankLessons[i]);
                }
                d.ranks = std::move(ranks);
                d.rankLessons = std::move(rankLessons);
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

    std::vector<uint32> const& Primaries()
    {
        static std::vector<uint32> const list = { ALCHEMY, BLACKSMITHING, ENCHANTING, ENGINEERING, HERBALISM,
                                                  LEATHERWORKING, MINING, SKINNING, TAILORING };
        return list;
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

    std::vector<TrainerRecipe> TrainerRecipes(uint32 skill)
    {
        std::vector<TrainerRecipe> out;
        if (SkillData const* data = DataOf(skill))
            for (RecipeLesson const& r : data->recipes)
                out.push_back({ r.spellId, r.lesson.reqSkillValue });
        return out;
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

    std::vector<Lesson> Available(Player const* player)
    {
        std::vector<Lesson> ranks, recipes;
        if (!player)
            return ranks;
        for (auto const& itr : Data())
        {
            uint32 const skill = itr.first;
            Kind const kind = KindOf(skill);
            if ((kind != Kind::Crafting && kind != Kind::Gathering) || !player->HasSkill(uint16(skill)))
                continue;
            SkillData const& d = itr.second;
            for (size_t i = 0; i < d.ranks.size(); ++i)
                if (d.ranks[i].maxValue < ARTISAN_MAX && player->GetTrainerSpellState(&d.rankLessons[i]) == TRAINER_SPELL_GREEN)
                    ranks.push_back({ skill, d.ranks[i].spellId, d.rankLessons[i].spellCost, true });
            for (RecipeLesson const& r : d.recipes)
                if (!player->HasSpell(r.spellId) && player->GetTrainerSpellState(&r.lesson) == TRAINER_SPELL_GREEN)
                    recipes.push_back({ skill, r.spellId, r.lesson.spellCost, false });
        }
        std::stable_sort(recipes.begin(), recipes.end(), [](Lesson const& a, Lesson const& b) { return a.cost < b.cost; });
        ranks.insert(ranks.end(), recipes.begin(), recipes.end());
        return ranks;
    }

    bool Teaches(uint32 creatureEntry, uint32 spellId)
    {
        CreatureInfo const* info = sObjectMgr.GetCreatureTemplate(creatureEntry);
        if (!info || !(info->npc_flags & UNIT_NPC_FLAG_TRAINER) || info->trainer_type != TRAINER_TYPE_TRADESKILLS)
            return false;
        TrainerSpellData const* own = sObjectMgr.GetNpcTrainerSpells(creatureEntry);
        TrainerSpellData const* shared = info->trainer_id ? sObjectMgr.GetNpcTrainerTemplateSpells(info->trainer_id) : nullptr;
        for (TrainerSpellData const* spells : { own, shared })
        {
            if (!spells)
                continue;
            for (auto const& entry : spells->spellList)
                for (uint32 taught : TaughtSpells(entry.second))
                    if (taught == spellId)
                        return true;
        }
        return false;
    }

    Paid LearnAt(Player* player, Creature const* trainer, uint32 keep)
    {
        Paid paid;
        if (!player || !trainer)
            return paid;
        // A rank makes new recipes green: again until nothing is learned (capped: bad trainer data).
        for (uint32 pass = 0; pass < 10; ++pass)
        {
            bool learnedAny = false;
            for (Lesson const& lesson : Available(player))
            {
                if (!Teaches(trainer->GetEntry(), lesson.spellId))
                    continue;
                if (player->GetMoney() < keep + lesson.cost)
                    continue;
                player->LearnSpell(lesson.spellId, false);
                if (lesson.cost)
                    player->ModifyMoney(-int32(lesson.cost));
                paid.cost += lesson.cost;
                ++paid.learned;
                learnedAny = true;
            }
            if (!learnedAny)
                break;
        }
        return paid;
    }

    bool ArtisanAvailable(Player const* player, uint32 skill)
    {
        SkillData const* d = DataOf(skill);
        if (!player || !d || !player->HasSkill(uint16(skill)))
            return false;
        for (size_t i = 0; i < d->ranks.size(); ++i)
            if (d->ranks[i].maxValue >= ARTISAN_MAX)
                return player->GetTrainerSpellState(&d->rankLessons[i]) == TRAINER_SPELL_GREEN;
        return false;
    }

    bool LearnArtisanAt(Player* player, Creature const* trainer, uint32 skill)
    {
        SkillData const* d = DataOf(skill);
        if (!player || !trainer || !d || !ArtisanAvailable(player, skill))
            return false;
        for (Rank const& rank : d->ranks)
        {
            if (rank.maxValue < ARTISAN_MAX)
                continue;
            if (!Teaches(trainer->GetEntry(), rank.spellId) || player->GetMoney() < rank.cost)
                return false;
            player->LearnSpell(rank.spellId, false);
            if (rank.cost)
                player->ModifyMoney(-int32(rank.cost));
            return true;
        }
        return false;
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

        for (std::vector<uint32> const* list : { &Primaries(), &Secondaries() })
            for (uint32 skill : *list)
                if (player->HasSkill(uint16(skill)))
                    player->SetSkill(uint16(skill), 0, 0);

        player->SetFreePrimaryProfessions(uint16(sWorld.getConfig(CONFIG_UINT32_MAX_PRIMARY_TRADE_SKILL)));
    }
}
}
