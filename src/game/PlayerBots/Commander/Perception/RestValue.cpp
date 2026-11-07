#include "Commander/Perception/RestValue.h"
#include "Commander/Perception/ValueModel.h"
#include "Commander/Perception/WorldModel.h"
#include <algorithm>

namespace Commander
{
namespace RestValue
{
    Worth Of(WorldModel const& world, PartyMemberFacts const& member, ManaWorth::Fight const& planned)
    {
        Worth w;
        if (!member.alive || member.inCombat)
            return w;
        // (one of us attacking something not fighting yet starts a fight too: WorldModel::targeted)
        bool const fighting = world.HasPartyEnemy() || !world.targeted.empty();

        if (member.UsesMana() && member.drinkManaPerSec > 0.0f &&
            float(member.mana) < float(member.maxMana) * FULL_SHARE)
        {
            w.drink = true;
            w.mana = float(member.maxMana) - float(member.mana);
            // While the drink is on, manaRegen already includes it (Player::UpdateManaRegen).
            float const rate = member.drinking ? member.manaRegen : member.drinkManaPerSec + member.manaRegen;
            w.sec = std::max(w.sec, w.mana / std::max(1.0f, rate));
        }
        if (!fighting && member.foodHealthPerSec > 0.0f &&
            float(member.health) < float(member.maxHealth) * FULL_SHARE)
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
}
}
