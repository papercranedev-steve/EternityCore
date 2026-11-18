#include "Shared/ClassQuestAbilities.h"
#include "Player.h"
#include "ObjectMgr.h"
#include "QuestDef.h"
#include "SpellMgr.h"
#include <algorithm>
#include <map>
#include <set>
#include <unordered_map>

namespace BotShared
{
    namespace
    {
        // Items some spell needs as a totem tool (Totem[]): Earth, Fire, Water, Air Totem...
        std::set<uint32> TotemTools()
        {
            std::set<uint32> tools;
            for (uint32 id = 0; id < sSpellMgr.GetMaxSpellId(); ++id)
                if (SpellEntry const* spell = sSpellMgr.GetSpellEntry(id))
                    for (uint32 tool : spell->Totem)
                        if (tool)
                            tools.insert(tool);
            return tools;
        }

        // What casting the quest's reward spell teaches (the spell Player::RewardQuest casts).
        std::vector<uint32> TaughtBy(Quest const* quest)
        {
            std::vector<uint32> spells;
            uint32 const castId = quest->GetRewSpellCast() ? quest->GetRewSpellCast() : quest->GetRewSpell();
            SpellEntry const* cast = castId ? sSpellMgr.GetSpellEntry(castId) : nullptr;
            if (!cast)
                return spells;
            for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
                if (cast->Effect[i] == SPELL_EFFECT_LEARN_SPELL && cast->EffectTriggerSpell[i])
                    spells.push_back(cast->EffectTriggerSpell[i]);
            return spells;
        }

        struct Catalog
        {
            std::vector<ClassQuestAbilities::Ability> abilities;
            // quest id -> entries of the creatures that give it
            std::unordered_map<uint32, std::vector<uint32>> givers;
        };

        Catalog Build()
        {
            Catalog catalog;
            std::set<uint32> const totemTools = TotemTools();

            // Variants are grouped by what they give.
            std::map<std::pair<std::vector<uint32>, std::vector<uint32>>, size_t> byReward;
            for (auto const& itr : sObjectMgr.GetQuestTemplates())
            {
                Quest const* quest = itr.second.get();
                if (!quest || !quest->GetRequiredClasses())
                    continue;

                std::vector<uint32> spells = TaughtBy(quest);
                std::vector<uint32> tools;
                for (uint32 i = 0; i < QUEST_REWARDS_COUNT; ++i)
                    if (quest->RewItemId[i] && totemTools.count(quest->RewItemId[i]))
                        tools.push_back(quest->RewItemId[i]);
                if (spells.empty() && tools.empty())
                    continue;

                auto const reward = std::make_pair(spells, tools);
                auto found = byReward.find(reward);
                if (found == byReward.end())
                {
                    ClassQuestAbilities::Ability ability;
                    ability.key = !spells.empty() ? spells.front() : tools.front();
                    ability.spells = std::move(spells);
                    ability.tools = std::move(tools);
                    found = byReward.emplace(reward, catalog.abilities.size()).first;
                    catalog.abilities.push_back(std::move(ability));
                }
                ClassQuestAbilities::Ability& ability = catalog.abilities[found->second];
                ability.classMask |= quest->GetRequiredClasses();
                ability.variants.push_back(quest);
            }

            std::set<uint32> rewardQuests;
            for (auto const& ability : catalog.abilities)
                for (Quest const* quest : ability.variants)
                    rewardQuests.insert(quest->GetQuestId());
            for (auto const& relation : sObjectMgr.GetCreatureQuestRelationsMap())
                if (rewardQuests.count(relation.second))
                    catalog.givers[relation.second].push_back(relation.first);
            return catalog;
        }

        Catalog const& GetCatalog()
        {
            static Catalog const catalog = Build();
            return catalog;
        }

        // Some creature gives the quest and none of them is friendly-or-neutral to the player.
        bool OnlyHostileGivers(Player const* player, Quest const* quest)
        {
            auto const itr = GetCatalog().givers.find(quest->GetQuestId());
            FactionTemplateEntry const* own = player->GetFactionTemplateEntry();
            if (itr == GetCatalog().givers.end() || !own)
                return false;
            for (uint32 entry : itr->second)
            {
                CreatureInfo const* info = sObjectMgr.GetCreatureTemplate(entry);
                FactionTemplateEntry const* faction = info ? sObjectMgr.GetFactionTemplateEntry(info->faction) : nullptr;
                if (!faction || !faction->IsHostileToPlayerTeam(*own))
                    return false;
            }
            return true;
        }
    }

    namespace ClassQuestAbilities
    {
        std::vector<Ability> const& All()
        {
            return GetCatalog().abilities;
        }

        Quest const* VariantFor(Player const* player, Ability const& ability)
        {
            if (!player || !(ability.classMask & player->GetClassMask()))
                return nullptr;
            Quest const* fallback = nullptr;
            for (Quest const* quest : ability.variants)
            {
                if (!player->SatisfyQuestClass(quest, false) || !player->SatisfyQuestRace(quest, false))
                    continue;
                if (!OnlyHostileGivers(player, quest))
                    return quest;
                if (!fallback)
                    fallback = quest;
            }
            return fallback;
        }

        bool IsEarned(Player const* player, Ability const& ability)
        {
            if (!player)
                return false;
            for (Quest const* quest : ability.variants)
                if (player->GetQuestRewardStatus(quest->GetQuestId()))
                    return true;
            return false;
        }

        void Revoke(Player* player, Ability const& ability)
        {
            if (!player)
                return;

            for (uint32 spellId : ability.spells)
            {
                if (!player->HasSpell(spellId))
                    continue;

                // A skill it teaches (Poisons): the skill goes with it (Player::RemoveSpell), and so
                // do the spells of that skill line (the recipes) that would otherwise stay usable.
                std::vector<uint32> skillSpells;
                if (SpellLearnSkillNode const* skill = sSpellMgr.GetSpellLearnSkill(spellId))
                    for (auto const& known : player->GetSpellMap())
                    {
                        if (known.second.state == PLAYERSPELL_REMOVED || known.first == spellId)
                            continue;
                        SkillLineAbilityMapBounds const bounds = sSpellMgr.GetSkillLineAbilityMapBoundsBySpellId(known.first);
                        for (auto itr = bounds.first; itr != bounds.second; ++itr)
                            if (itr->second->skillId == skill->skill)
                            {
                                skillSpells.push_back(known.first);
                                break;
                            }
                    }

                player->RemoveSpell(spellId, false, false);   // higher ranks too
                for (uint32 other : skillSpells)
                    player->RemoveSpell(other, false, false);
            }

            for (uint32 tool : ability.tools)
                if (uint32 const count = player->GetItemCount(tool, true))
                    player->DestroyItemCount(tool, count, true, false, true);
        }
    }
}
