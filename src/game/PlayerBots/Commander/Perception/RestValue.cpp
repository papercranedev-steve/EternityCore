#include "Commander/Perception/RestValue.h"
#include "Commander/Perception/ValueModel.h"
#include "Commander/Perception/WorldModel.h"
#include <algorithm>

namespace Commander
{
namespace RestValue
{
    Worth OfDrinking(WorldModel const& world, PartyMemberFacts const& member, ManaWorth::Fight const& planned)
    {
        Worth w;
        if (!member.alive || !member.UsesMana() || member.drinkManaPerSec <= 0.0f)
            return w;
        if (float(member.mana) >= float(member.maxMana) * FULL_SHARE)
            return w;

        w.mana = float(member.maxMana) - float(member.mana);
        // While the drink is on, manaRegen already includes it (Player::UpdateManaRegen).
        float const rate = member.drinking ? member.manaRegen : member.drinkManaPerSec + member.manaRegen;
        w.sec = w.mana / std::max(1.0f, rate);

        bool const live = world.rates.incomingDps > 0.0f || planned.sec <= 0.0f;
        float const incoming = live ? world.rates.incomingDps : planned.incomingDps;
        w.benefit = live ? ManaWorth::Points(world, member, w.mana)
                         : ManaWorth::Points(world, member, w.mana, planned);
        bool const healer = member.isBot && member.role == ROLE_HEALER;
        w.cost = healer ? 0.0f : Value::ContributionPerSecAt(world, member, incoming) * w.sec;
        return w;
    }
}
}
