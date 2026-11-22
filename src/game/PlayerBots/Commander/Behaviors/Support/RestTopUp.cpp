#include "Commander/Behaviors/Support/RestTopUp.h"
#include "Commander/Behaviors/Support/HealPlan.h"
#include "Commander/Perception/Values/HealthRegen.h"
#include "Commander/Team.h"
#include "TeamBotAI.h"
#include "HealSpells.h"
#include "Player.h"
#include "Pet.h"
#include "Timer.h"
#include <algorithm>
#include <vector>

namespace Commander
{
namespace RestTopUp
{
    namespace
    {
        // The heal of the castable option with the most healing per mana; 0 = none.
        float EfficientHeal(HealChoice const& choice)
        {
            float best = 0.0f;
            float bestPerMana = -1.0f;
            for (HealSpells::Option const& o : choice.options)
            {
                float const heal = o.amount.direct + o.amount.periodic;
                if (heal <= 0.0f)
                    continue;
                float const perMana = o.mana > 0.0f ? heal / o.mana : HealPlan::NO_LIMIT;
                if (perMana > bestPerMana)
                {
                    bestPerMana = perMana;
                    best = heal;
                }
            }
            return best;
        }
    }

    float Deficit(float maxHealth, float efficientHeal)
    {
        return std::min(efficientHeal * (1.0f - HealPlan::OVERHEAL_MAX), maxHealth * MAX_DEFICIT_PCT / 100.0f);
    }

    Unit* Select(TeamBotAI const* bot, RestTopUpState& state, float selfPercent, float groupPercent)
    {
        Player* me = bot->me;
        uint32 const now = WorldTimer::getMSTime();

        std::vector<Unit*> units;
        units.push_back(me);
        for (Player* member : Team::Members(me))
        {
            if (member == me || !me->IsInMap(member))
                continue;
            units.push_back(member);
        }
        // Pets after the players (a pet only when no player needs a heal: lowest percent wins
        // among players first, as SelectHealTarget).
        size_t const players = units.size();
        for (size_t i = 0; i < players; ++i)
            if (Player* owner = units[i]->ToPlayer())
                if (Pet* pet = owner->GetPet())
                    units.push_back(pet);

        std::map<ObjectGuid, RestTopUpState::Gap> gaps;
        Unit* target = nullptr;
        bool targetIsPlayer = false;
        for (Unit* unit : units)
        {
            bool const isPlayer = unit->IsPlayer();
            if (target && targetIsPlayer && !isPlayer)
                break;
            if (!bot->IsValidHealTarget(unit, unit == me ? selfPercent : groupPercent))
                continue;

            // (the gap's clock runs for every member, also one not chosen this tick)
            if (!unit->IsInCombat())
            {
                HealChoice choice;
                bot->ChooseHeal(unit, choice);
                float const maxHealth = float(unit->GetMaxHealth());
                float const missing = maxHealth - float(unit->GetHealth());
                float const efficient = EfficientHeal(choice);
                if (efficient <= 0.0f || missing < Deficit(maxHealth, efficient) ||
                    HealthRegen::SecondsToFull(unit) < WAIT_SEC)
                    continue;

                RestTopUpState::Gap gap;
                gap.maxHealth = unit->GetMaxHealth();
                gap.sinceMs = now;
                auto const seen = state.gaps.find(unit->GetObjectGuid());
                if (seen != state.gaps.end() && seen->second.maxHealth == gap.maxHealth)
                    gap.sinceMs = seen->second.sinceMs;
                gaps[unit->GetObjectGuid()] = gap;
                if (WorldTimer::getMSTimeDiff(gap.sinceMs, now) < STABLE_MS)
                    continue;
            }

            if (target && unit != me && !TeamBotAI::IsTankClass(unit->GetClass()) &&
                bot->AreOthersOnSameTarget(unit->GetObjectGuid(), false, true))
                continue;
            if (target && target->GetHealthPercent() <= unit->GetHealthPercent())
                continue;

            target = unit;
            targetIsPlayer = isPlayer;
        }
        state.gaps.swap(gaps);
        return target;
    }
}
}
