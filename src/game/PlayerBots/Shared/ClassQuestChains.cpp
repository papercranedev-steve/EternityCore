#include "Shared/ClassQuestChains.h"
#include "Shared/ClassQuestAbilities.h"
#include "Shared/QuestGrant.h"
#include "Shared/QuestMirror.h"
#include "Player.h"
#include "ObjectMgr.h"
#include "QuestDef.h"
#include "Database/DatabaseEnv.h"
#include "Log.h"
#include <algorithm>
#include <map>
#include <unordered_map>
#include <unordered_set>

namespace BotShared
{
    namespace
    {
        std::vector<ClassQuestChains::Chain> g_chains;
        std::unordered_map<uint32, size_t> g_byId;
        std::unordered_map<uint32, std::vector<uint32>> g_givers;  // quest -> creatures that give it

        // How the creatures that give `quest` stand to the player's team: 2 friendly (one is),
        // 1 neutral or nobody gives it, 0 hostile (all are).
        int GiverStanding(Player const* player, Quest const* quest)
        {
            auto const itr = g_givers.find(quest->GetQuestId());
            FactionTemplateEntry const* own = player->GetFactionTemplateEntry();
            if (itr == g_givers.end() || !own)
                return 1;
            int best = 0;
            for (uint32 entry : itr->second)
            {
                CreatureInfo const* info = sObjectMgr.GetCreatureTemplate(entry);
                FactionTemplateEntry const* faction = info ? sObjectMgr.GetFactionTemplateEntry(info->faction) : nullptr;
                if (!faction)
                    best = std::max(best, 1);
                else if (faction->IsFriendlyTo(*own))
                    best = 2;
                else if (!faction->IsHostileToPlayerTeam(*own))
                    best = std::max(best, 1);
            }
            return best;
        }

        // The ability keys a chain's steps give, sorted (empty for a gear chain).
        std::vector<uint32> AbilityKeys(ClassQuestChains::Chain const& chain)
        {
            std::vector<uint32> keys;
            for (uint32 questId : chain.quests)
                if (ClassQuestAbilities::Ability const* ability = ClassQuestAbilities::AbilityOf(questId))
                    keys.push_back(ability->key);
            std::sort(keys.begin(), keys.end());
            keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
            return keys;
        }

        bool IsClassQuest(Quest const* quest)
        {
            return quest && quest->GetRequiredClasses() && !quest->GetBreadcrumbForQuestId() &&
                   quest->GetQuestId() < QuestMirror::MIRROR_OFFSET;
        }

        // What the trainers send a class on, as opposed to the class's raid and dungeon token
        // turn-ins (zone quests: Conqueror's / Dreadnaught sets, Paragons of Power, T0.5): filed
        // under the class in the quest log, or rewarding a class quest ability (Vile Familiars,
        // Arcane Refreshment are zone quests).
        bool IsChainQuest(Quest const* quest)
        {
            if (!IsClassQuest(quest))
                return false;
            int32 const sort = quest->GetZoneOrSort();
            return (sort < 0 && ClassByQuestSort(-sort) != 0) || ClassQuestAbilities::AbilityOf(quest->GetQuestId());
        }

        bool RacesOverlap(uint32 a, uint32 b)
        {
            return !a || !b || (a & b);
        }

        bool RewardsItems(Quest const* quest)
        {
            for (uint32 i = 0; i < QUEST_REWARDS_COUNT; ++i)
                if (quest->RewItemId[i])
                    return true;
            for (uint32 i = 0; i < QUEST_REWARD_CHOICES_COUNT; ++i)
                if (quest->RewChoiceItemId[i])
                    return true;
            return false;
        }

        uint32 DefaultAffinity(ClassQuestChains::Kind kind, size_t steps)
        {
            if (kind == ClassQuestChains::Kind::Gear)
                return 1;
            return steps <= 1 ? 2 : steps <= 4 ? 4 : 6;
        }

        struct Override
        {
            int32 kind = -1;        // -1 = auto
            int32 affinity = -1;    // -1 = auto
            bool skip = false;
            std::string comment;
        };

        std::unordered_map<uint32, Override> LoadOverrides()
        {
            std::unordered_map<uint32, Override> overrides;
            std::unique_ptr<QueryResult> exists(WorldDatabase.Query("SHOW TABLES LIKE 'custom_classquest_chain'"));
            if (!exists)
            {
                sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, ">> [ClassQuestChains] No table `custom_classquest_chain`: defaults only.");
                return overrides;
            }
            std::unique_ptr<QueryResult> result(WorldDatabase.Query(
                "SELECT `last_quest`, IFNULL(`kind`, -1), IFNULL(`affinity`, -1), `skip`, `comment` FROM `custom_classquest_chain`"));
            if (!result)
                return overrides;
            do
            {
                Field* f = result->Fetch();
                Override& o = overrides[f[0].GetUInt32()];
                o.kind = f[1].GetInt32();
                o.affinity = f[2].GetInt32();
                o.skip = f[3].GetUInt8() != 0;
                o.comment = f[4].GetCppString();
            } while (result->NextRow());
            return overrides;
        }
    }

    namespace ClassQuestChains
    {
        void Load()
        {
            g_chains.clear();
            g_byId.clear();
            g_givers.clear();
            for (auto const& relation : sObjectMgr.GetCreatureQuestRelationsMap())
                g_givers[relation.second].push_back(relation.first);

            std::vector<Quest const*> classQuests;
            for (auto const& itr : sObjectMgr.GetQuestTemplates())
                if (IsChainQuest(itr.second.get()))
                    classQuests.push_back(itr.second.get());

            // Continued: some class quest comes after it.
            std::unordered_set<uint32> continued;
            for (Quest const* quest : classQuests)
            {
                if (quest->GetPrevQuestId() > 0 && IsChainQuest(sObjectMgr.GetQuestTemplate(uint32(quest->GetPrevQuestId()))))
                    continued.insert(uint32(quest->GetPrevQuestId()));
                if (IsChainQuest(sObjectMgr.GetQuestTemplate(quest->GetNextQuestInChain())))
                    continued.insert(quest->GetQuestId());
            }

            std::unordered_map<uint32, Override> const overrides = LoadOverrides();
            uint32 skipped = 0, outsideStart = 0, nothing = 0;
            for (Quest const* last : classQuests)
            {
                if (continued.count(last->GetQuestId()))
                    continue;

                uint32 const classMask = last->GetRequiredClasses();
                uint32 const raceMask = last->GetRequiredRaces();
                std::vector<Quest const*> const path = QuestGrant::ChainUpTo(last, [&](Quest const* q)
                {
                    return IsChainQuest(q) && (q->GetRequiredClasses() & classMask) && RacesOverlap(q->GetRequiredRaces(), raceMask);
                });

                // A chain that goes on after its last ability (Defensive Stance, then a weapon) is two:
                // the ability chain ends with that quest, the rest is a gear chain after it.
                size_t abilityEnd = 0;  // 1 + index of the last ability step, 0 = none
                for (size_t i = 0; i < path.size(); ++i)
                    if (ClassQuestAbilities::AbilityOf(path[i]->GetQuestId()))
                        abilityEnd = i + 1;

                auto add = [&](size_t from, size_t to, bool afterAbility)
                {
                    Quest const* end = path[to - 1];
                    if (g_byId.count(end->GetQuestId()))
                        return;     // a shared start, already made from another last quest
                    bool items = false;
                    for (size_t i = from; i < to; ++i)
                        items = items || RewardsItems(path[i]);

                    Chain chain;
                    chain.id = end->GetQuestId();
                    chain.classMask = classMask;
                    chain.raceMask = raceMask;
                    chain.minLevel = path[from]->GetMinLevel();
                    chain.afterAbility = afterAbility;
                    chain.after = afterAbility ? path[from - 1]->GetQuestId() : 0;
                    for (size_t i = from; i < to; ++i)
                        chain.quests.push_back(path[i]->GetQuestId());

                    auto const o = overrides.find(chain.id);
                    if (o != overrides.end() && o->second.skip)
                    {
                        ++skipped;
                        return;
                    }
                    if (o != overrides.end() && o->second.kind >= 0)
                        chain.kind = o->second.kind == int32(Kind::Ability) ? Kind::Ability : Kind::Gear;
                    else if (!afterAbility && abilityEnd)
                        chain.kind = Kind::Ability;
                    else if (items)
                        chain.kind = Kind::Gear;
                    else
                    {
                        ++nothing;
                        return;
                    }
                    chain.affinity = o != overrides.end() && o->second.affinity >= 0 ? uint32(o->second.affinity) :
                        DefaultAffinity(chain.kind, to - from);
                    if (o != overrides.end())
                        chain.comment = o->second.comment;

                    // The first class quest needs a quest that is not a class quest (the bot's own business).
                    if (!afterAbility && path.front()->GetPrevQuestId() > 0)
                        ++outsideStart;

                    g_byId[chain.id] = g_chains.size();
                    g_chains.push_back(std::move(chain));
                };

                if (abilityEnd)
                {
                    add(0, abilityEnd, false);
                    if (abilityEnd < path.size())
                        add(abilityEnd, path.size(), true);
                }
                else
                    add(0, path.size(), false);
            }

            uint32 abilityChains = 0;
            for (Chain const& chain : g_chains)
                abilityChains += chain.kind == Kind::Ability ? 1 : 0;
            sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, ">> [ClassQuestChains] %u class quest chains (%u ability, %u gear),"
                " %u left out by `custom_classquest_chain`, %u reward nothing, %u start after a quest of no class.",
                uint32(g_chains.size()), abilityChains, uint32(g_chains.size()) - abilityChains, skipped, nothing, outsideStart);
        }

        std::vector<Chain> const& All()
        {
            return g_chains;
        }

        Chain const* Find(uint32 chainId)
        {
            auto const itr = g_byId.find(chainId);
            return itr != g_byId.end() ? &g_chains[itr->second] : nullptr;
        }

        std::vector<Quest const*> StepsFor(Player const* player, Chain const& chain)
        {
            Quest const* last = sObjectMgr.GetQuestTemplate(chain.id);
            if (!player || !last || !player->SatisfyQuestClass(last, false) || !player->SatisfyQuestRace(last, false))
                return {};
            std::vector<Quest const*> steps = QuestGrant::ChainUpTo(last, [player](Quest const* q)
            {
                return IsChainQuest(q) && player->SatisfyQuestClass(q, false) && player->SatisfyQuestRace(q, false);
            });
            if (steps.empty() || GiverStanding(player, steps.front()) == 0)
                return {};
            if (chain.afterAbility)
            {
                auto lastAbility = std::find_if(steps.rbegin(), steps.rend(), [](Quest const* q)
                {
                    return ClassQuestAbilities::AbilityOf(q->GetQuestId()) != nullptr;
                });
                if (lastAbility != steps.rend())
                    steps.erase(steps.begin(), lastAbility.base());
                if (steps.empty())
                    return {};
            }
            return steps;
        }

        std::vector<Chain const*> ChainsFor(Player const* player)
        {
            std::vector<Chain const*> chains;
            // ability keys -> (index in chains, how friendly its first giver is)
            std::map<std::vector<uint32>, std::pair<size_t, int>> byAbilities;
            for (Chain const& chain : g_chains)
            {
                std::vector<Quest const*> const steps = StepsFor(player, chain);
                if (steps.empty())
                    continue;
                if (chain.kind != Kind::Ability)
                {
                    chains.push_back(&chain);
                    continue;
                }
                int const standing = GiverStanding(player, steps.front());
                std::vector<uint32> keys = AbilityKeys(chain);
                auto const same = byAbilities.find(keys);
                if (same == byAbilities.end())
                {
                    byAbilities.emplace(std::move(keys), std::make_pair(chains.size(), standing));
                    chains.push_back(&chain);
                }
                else if (standing > same->second.second)
                {
                    chains[same->second.first] = &chain;
                    same->second.second = standing;
                }
            }

            // A gear chain after an ability chain only follows the one picked (the weapon after
            // the Stormwind Defensive Stance, not the ones after the other start zones').
            std::unordered_set<uint32> picked;
            for (Chain const* chain : chains)
                if (chain->kind == Kind::Ability)
                    picked.insert(chain->id);
            chains.erase(std::remove_if(chains.begin(), chains.end(), [&](Chain const* chain)
            {
                return chain->after && !picked.count(chain->after);
            }), chains.end());
            return chains;
        }

        bool IsDone(Player const* player, Chain const& chain)
        {
            return player && player->GetQuestRewardStatus(chain.id);
        }

        bool GrantDone(Player* player, Chain const& chain)
        {
            std::vector<Quest const*> const steps = StepsFor(player, chain);
            if (steps.empty())
                return false;
            QuestGrant::MarkChainRewarded(player, steps.back());
            if (chain.kind == Kind::Ability)
                for (Quest const* step : steps)
                    if (ClassQuestAbilities::AbilityOf(step->GetQuestId()))
                        QuestGrant::GiveRewards(player, step);
            return true;
        }
    }
}
