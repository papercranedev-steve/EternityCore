#include "Commander/Perception/ManaWorth.h"
#include "Commander/Perception/ManaBudget.h"
#include "Commander/Perception/ValueModel.h"
#include "Commander/Perception/WorldModel.h"
#include <algorithm>

namespace Commander
{
namespace ManaWorth
{
    namespace
    {
        bool IsHealer(PartyMemberFacts const& m)
        {
            return m.isBot && !m.isPet && m.role == ROLE_HEALER;
        }

        float HealPerMana(PartyMemberFacts const& m)
        {
            float sum = 0.0f;
            uint32 count = 0;
            for (HealRate const& r : m.healRates)
            {
                if (r.perMana <= 0.0f)
                    continue;
                sum += r.perMana;
                ++count;
            }
            return count ? sum / float(count) : ManaBudget::FALLBACK_HEAL_PER_MANA;
        }

        // ManaBudgetBehavior::ComputeBudget's headroom: health above the floor of the members
        // being hit.
        float Headroom(WorldModel const& world)
        {
            float headroom = 0.0f;
            for (PartyMemberFacts const& m : world.party)
            {
                if (!m.alive || Value::IncomingOn(world, m.guid) <= 0.0f)
                    continue;
                headroom += std::max(0.0f, float(m.health) - float(m.maxHealth) * ManaBudget::HEADROOM_FLOOR_PCT);
            }
            return headroom;
        }

        float TensionWith(WorldModel const& world, PartyMemberFacts const& m, float mana)
        {
            float const fightSec = Value::FightRemainingSec(world);
            if (IsHealer(m))
            {
                float const needed = ManaBudget::NeededHealing(world.rates.incomingDps, fightSec, Headroom(world),
                                                               world.rates.healers);
                return ManaBudget::Tension(ManaBudget::NeededMana(needed, HealPerMana(m)), mana);
            }
            if (m.fillerManaPerSec <= 0.0f)
                return 0.0f;
            // Fight left / seconds the mana lasts = mana needed / mana.
            return ManaBudget::Tension(fightSec * m.fillerManaPerSec, mana);
        }

        float Scarce(float tension)
        {
            return std::max(SCARCITY_FLOOR, ManaBudget::Scarcity(tension));
        }
    }

    float Tension(WorldModel const& world, PartyMemberFacts const& member)
    {
        if (!member.UsesMana())
            return 0.0f;
        return TensionWith(world, member, float(member.mana));
    }

    float Points(WorldModel const& world, PartyMemberFacts const& member, float mana)
    {
        if (mana <= 0.0f || !member.UsesMana())
            return 0.0f;
        // Scarcity after paying it: the last mana is the dearest.
        float const scarcity = Scarce(TensionWith(world, member, std::max(0.0f, float(member.mana) - mana)));
        if (IsHealer(member))
            return Value::Points(world, mana * HealPerMana(member)) * scarcity;
        if (member.fillerManaPerSec <= 0.0f)
            return 0.0f;
        return Value::LostAction(world, member, mana / member.fillerManaPerSec) * scarcity;
    }
}
}
