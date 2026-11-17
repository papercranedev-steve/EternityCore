#include "Shared/ClassTraining.h"
#include "Player.h"
#include "ObjectMgr.h"
#include "SpellMgr.h"
#include <algorithm>
#include <map>
#include <set>
#include <vector>

namespace BotShared
{
    namespace
    {
        // Every lesson of the class trainers of one class, cheapest first. Built once per class;
        // copies, so a trainer reload does not leave dangling pointers (it is just not picked up).
        std::vector<TrainerSpell> const& LessonsOf(uint8 classId)
        {
            static std::map<uint8, std::vector<TrainerSpell>> cache;
            auto itr = cache.find(classId);
            if (itr != cache.end())
                return itr->second;

            std::vector<TrainerSpell>& lessons = cache[classId];
            std::set<uint32> seenSpells, seenTemplates;
            auto add = [&](TrainerSpellData const* data)
            {
                if (!data)
                    return;
                for (auto const& entry : data->spellList)
                    if (seenSpells.insert(entry.second.spell).second)
                        lessons.push_back(entry.second);
            };

            for (auto const& info : sObjectMgr.GetCreatureInfoMap())
            {
                CreatureInfo const* cInfo = info.second.get();
                if (!cInfo || !(cInfo->npc_flags & UNIT_NPC_FLAG_TRAINER))
                    continue;
                if (cInfo->trainer_type != TRAINER_TYPE_CLASS || cInfo->trainer_class != classId)
                    continue;
                add(sObjectMgr.GetNpcTrainerSpells(info.first));
                if (cInfo->trainer_id && seenTemplates.insert(cInfo->trainer_id).second)
                    add(sObjectMgr.GetNpcTrainerTemplateSpells(cInfo->trainer_id));
            }

            std::sort(lessons.begin(), lessons.end(), [](TrainerSpell const& a, TrainerSpell const& b)
            {
                return a.spellCost != b.spellCost ? a.spellCost < b.spellCost : a.spell < b.spell;
            });
            return lessons;
        }

        // The trainer spell teaches through its trigger spells (same as the .learn all_trainer helper).
        bool Learn(Player* player, TrainerSpell const& lesson)
        {
            SpellEntry const* teach = sSpellMgr.GetSpellEntry(lesson.spell);
            if (!teach)
                return false;
            bool learned = false;
            for (uint32 spellId : teach->EffectTriggerSpell)
            {
                if (!spellId || sSpellMgr.IsPrimaryProfessionFirstRankSpell(spellId))
                    continue;
                if (!player->IsSpellFitByClassAndRace(spellId))
                    continue;
                player->LearnSpell(spellId, false);
                learned = true;
            }
            return learned;
        }
    }

    namespace ClassTraining
    {
        Result LearnAvailable(Player* player, bool pay)
        {
            Result result;
            if (!player)
                return result;

            std::vector<TrainerSpell> const& lessons = LessonsOf(player->GetClass());
            // Lessons are not in rank order: a learned rank makes the next one available, loop again.
            // A cap on passes in case a lesson stays available after learning it (bad trainer data).
            bool learnedAny;
            uint32 passes = 0;
            do
            {
                if (++passes > 20)
                    break;
                learnedAny = false;
                for (TrainerSpell const& lesson : lessons)
                {
                    if (player->GetTrainerSpellState(&lesson) != TRAINER_SPELL_GREEN)
                        continue;
                    if (pay && player->GetMoney() < lesson.spellCost)
                        continue;
                    if (!Learn(player, lesson))
                        continue;
                    if (pay && lesson.spellCost)
                    {
                        player->ModifyMoney(-int32(lesson.spellCost));
                        result.cost += lesson.spellCost;
                    }
                    ++result.learned;
                    learnedAny = true;
                }
            } while (learnedAny);

            if (pay)
                for (TrainerSpell const& lesson : lessons)
                    if (player->GetTrainerSpellState(&lesson) == TRAINER_SPELL_GREEN)
                        ++result.tooExpensive;
            return result;
        }

        bool CheapestAvailable(Player const* player, uint32& cost)
        {
            if (!player)
                return false;
            // Lessons are sorted cheapest first.
            for (TrainerSpell const& lesson : LessonsOf(player->GetClass()))
                if (player->GetTrainerSpellState(&lesson) == TRAINER_SPELL_GREEN)
                {
                    cost = lesson.spellCost;
                    return true;
                }
            return false;
        }

        uint32 CostOfAvailable(Player const* player)
        {
            uint32 cost = 0;
            if (!player)
                return cost;
            for (TrainerSpell const& lesson : LessonsOf(player->GetClass()))
                if (player->GetTrainerSpellState(&lesson) == TRAINER_SPELL_GREEN)
                    cost += lesson.spellCost;
            return cost;
        }
    }
}
