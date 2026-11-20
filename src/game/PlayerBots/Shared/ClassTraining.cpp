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

        // The spell the lesson teaches this player (its trainer spell's first trigger spell
        // of the player's class and race), 0 = none.
        uint32 TaughtSpell(Player const* player, TrainerSpell const& lesson)
        {
            SpellEntry const* teach = sSpellMgr.GetSpellEntry(lesson.spell);
            if (!teach)
                return 0;
            for (uint32 spellId : teach->EffectTriggerSpell)
                if (spellId && !sSpellMgr.IsPrimaryProfessionFirstRankSpell(spellId) && player->IsSpellFitByClassAndRace(spellId))
                    return spellId;
            return 0;
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

        std::vector<Lesson> Available(Player const* player)
        {
            std::vector<Lesson> available;
            if (!player)
                return available;
            for (TrainerSpell const& lesson : LessonsOf(player->GetClass()))
            {
                if (player->GetTrainerSpellState(&lesson) != TRAINER_SPELL_GREEN)
                    continue;
                if (uint32 const spellId = TaughtSpell(player, lesson))
                    available.push_back({ spellId, lesson.spellCost });
            }
            return available;
        }

        Result LearnInOrder(Player* player, std::function<uint32(uint32 spellId)> const& rank, uint32 skipFrom)
        {
            Result result;
            if (!player)
                return result;

            std::vector<TrainerSpell> const& lessons = LessonsOf(player->GetClass());
            // A learned rank makes the next one available: again until nothing is learned
            // (capped in case a lesson stays available after learning it: bad trainer data).
            for (uint32 pass = 0; pass < 20; ++pass)
            {
                struct Ranked
                {
                    TrainerSpell const* lesson;
                    uint32 rank;
                };
                std::vector<Ranked> green;
                uint32 total = 0;
                for (TrainerSpell const& lesson : lessons)
                {
                    if (player->GetTrainerSpellState(&lesson) != TRAINER_SPELL_GREEN)
                        continue;
                    uint32 const spellId = TaughtSpell(player, lesson);
                    if (!spellId)
                        continue;
                    green.push_back({ &lesson, rank(spellId) });
                    total += lesson.spellCost;
                }
                std::stable_sort(green.begin(), green.end(), [](Ranked const& a, Ranked const& b)
                {
                    return a.rank != b.rank ? a.rank < b.rank : a.lesson->spellCost < b.lesson->spellCost;
                });

                bool const short_ = player->GetMoney() < total;
                bool learnedAny = false;
                for (Ranked const& r : green)
                {
                    if (short_ && r.rank >= skipFrom)
                        continue;
                    if (player->GetMoney() < r.lesson->spellCost || !Learn(player, *r.lesson))
                        continue;
                    if (r.lesson->spellCost)
                    {
                        player->ModifyMoney(-int32(r.lesson->spellCost));
                        result.cost += r.lesson->spellCost;
                    }
                    ++result.learned;
                    learnedAny = true;
                }
                if (!learnedAny)
                    break;
            }

            for (TrainerSpell const& lesson : lessons)
                if (player->GetTrainerSpellState(&lesson) == TRAINER_SPELL_GREEN)
                    ++result.tooExpensive;
            return result;
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
