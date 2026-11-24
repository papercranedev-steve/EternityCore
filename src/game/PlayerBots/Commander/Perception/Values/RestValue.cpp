#include "Commander/Perception/Values/RestValue.h"
#include "Commander/Perception/Values/ValueModel.h"
#include "Commander/Perception/WorldModel.h"
#include <algorithm>

namespace Commander
{
namespace RestValue
{
    float DrinkStartShare(FightManaSpendState const& spend, bool dungeon)
    {
        float average = 0.0f;
        if (!FightManaSpend::Average(spend, dungeon, MIN_FIGHTS, average))
            return dungeon ? DUNGEON_DRINK_SHARE : OPEN_WORLD_DRINK_SHARE;
        return std::min(START_SHARE, std::max(MIN_DRINK_SHARE, FIGHTS_MARGIN * average));
    }

    float DrinkBelowShare(PartyMemberFacts const& member, float pullReadyShare)
    {
        if (member.drinking)
            return FULL_SHARE;
        return std::max(member.drinkStartShare, pullReadyShare);
    }

    Worth Of(WorldModel const& world, PartyMemberFacts const& member, ManaWorth::Fight const& planned,
             float pullReadyShare)
    {
        Worth w;
        if (!member.alive || member.inCombat)
            return w;
        // (one of us attacking something not fighting yet starts a fight too: WorldModel::targeted)
        bool const fighting = world.HasPartyEnemy() || !world.targeted.empty();

        if (member.UsesMana() && member.drinkManaPerSec > 0.0f &&
            float(member.mana) < float(member.maxMana) * DrinkBelowShare(member, pullReadyShare))
        {
            w.drink = true;
            w.mana = float(member.maxMana) - float(member.mana);
            // While the drink is on, manaRegen already includes it (Player::UpdateManaRegen).
            float const rate = member.drinking ? member.manaRegen : member.drinkManaPerSec + member.manaRegen;
            w.sec = std::max(w.sec, w.mana / std::max(1.0f, rate));
        }
        if (!fighting && member.foodHealthPerSec > 0.0f &&
            float(member.health) < float(member.maxHealth) * (member.eating ? FULL_SHARE : START_SHARE))
        {
            w.eat = true;
            w.health = float(member.maxHealth) - float(member.health);
            w.sec = std::max(w.sec, w.health / member.foodHealthPerSec);
        }
        if (!w.Any())
            return w;

        bool const live = fighting && world.rates.incomingDps > 0.0f;
        if (!live && planned.sec <= 0.0f)
        {
            w.free = !fighting;
            if (w.free)
                return w;
        }

        float const incoming = live ? world.rates.incomingDps : planned.incomingDps;
        if (w.drink)
            w.benefit += live || planned.sec <= 0.0f ? ManaWorth::Points(world, member, w.mana)
                                                     : ManaWorth::Points(world, member, w.mana, planned);
        if (w.eat)
            w.benefit += Value::Points(world, w.health);
        bool const healer = member.isBot && member.role == ROLE_HEALER;
        w.cost = healer ? 0.0f : Value::ContributionPerSecAt(world, member, incoming) * w.sec;
        return w;
    }

    float SecondsToFull(PartyMemberFacts const& member, float health, float mana)
    {
        float sec = 0.0f;
        if (member.UsesMana() && member.drinkManaPerSec > 0.0f)
        {
            // While the drink is on, manaRegen already includes it (as in Of).
            float const rate = member.drinking ? member.manaRegen : member.drinkManaPerSec + member.manaRegen;
            sec = std::max(sec, std::max(0.0f, float(member.maxMana) - mana) / std::max(1.0f, rate));
        }
        if (member.foodHealthPerSec > 0.0f)
            sec = std::max(sec, std::max(0.0f, float(member.maxHealth) - health) / member.foodHealthPerSec);
        return sec;
    }

    float TapSavesSec(WorldModel const& world, PartyMemberFacts const& member)
    {
        if (!member.alive || member.inCombat || world.HasPartyEnemy() || !world.targeted.empty())
            return 0.0f;
        if (!member.UsesMana() || member.drinkManaPerSec <= 0.0f || member.foodHealthPerSec <= 0.0f ||
            member.tapManaPerCast <= 0.0f || member.tapManaPerHealth <= 0.0f)
            return 0.0f;
        float const health = float(member.health);
        float const mana = float(member.mana);
        float const gain = member.tapManaPerCast;
        float const healthCost = gain / member.tapManaPerHealth;
        if (float(member.maxMana) - mana < gain || health <= healthCost)
            return 0.0f;
        return std::max(0.0f, SecondsToFull(member, health, mana) -
                              SecondsToFull(member, health - healthCost, mana + gain));
    }
}
}
