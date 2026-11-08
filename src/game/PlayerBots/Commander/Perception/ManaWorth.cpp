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

        // The fight the mana is for: the current one (live rates), or one planned (Fight).
        struct Context
        {
            float fightSec = 0.0f;
            float incomingDps = 0.0f;
            float headroom = 0.0f;
        };

        Context Live(WorldModel const& world)
        {
            Context c;
            c.fightSec = Value::FightRemainingSec(world);
            c.incomingDps = world.rates.incomingDps;
            c.headroom = Headroom(world);
            return c;
        }

        Context Planned(Fight const& fight)
        {
            Context c;
            c.fightSec = fight.sec;
            c.incomingDps = fight.incomingDps;
            return c;
        }

        float TensionWith(WorldModel const& world, Context const& c, PartyMemberFacts const& m, float mana)
        {
            if (IsHealer(m))
            {
                float const needed = ManaBudget::NeededHealing(c.incomingDps, c.fightSec, c.headroom, world.rates.healers);
                return ManaBudget::Tension(ManaBudget::NeededMana(needed, HealPerMana(m)), mana);
            }
            if (m.fillerManaPerSec <= 0.0f)
                return 0.0f;
            // Fight left / seconds the mana lasts = mana needed / mana.
            return ManaBudget::Tension(c.fightSec * m.fillerManaPerSec, mana);
        }

        float Scarce(float tension)
        {
            return std::max(SCARCITY_FLOOR, ManaBudget::Scarcity(tension));
        }

        float WorthIn(WorldModel const& world, Context const& c, PartyMemberFacts const& member, float mana)
        {
            if (mana <= 0.0f || !member.UsesMana())
                return 0.0f;
            // Scarcity after paying it: the last mana is the dearest.
            float const scarcity = Scarce(TensionWith(world, c, member, std::max(0.0f, float(member.mana) - mana)));
            if (IsHealer(member))
                return Value::Points(world, mana * HealPerMana(member)) * scarcity;
            if (member.fillerManaPerSec <= 0.0f)
                return 0.0f;
            // Seconds of casting the mana pays for, at this member's share of the fight
            // (Value::ContributionPerSec with the fight's incoming damage).
            float const share = 1.0f / float(std::max(1u, world.rates.damageDealers));
            float const worth = Value::Points(world, c.incomingDps) * share * (mana / member.fillerManaPerSec) * scarcity;
            // A warlock taps it back: the health it costs and the global cooldowns (header).
            if (member.tapManaPerHealth > 0.0f && member.tapManaPerCast > 0.0f)
                return std::min(worth, Value::Points(world, mana / member.tapManaPerHealth) +
                                       Value::LostAction(world, member, Value::GCD_SEC * mana / member.tapManaPerCast));
            return worth;
        }
    }

    float Tension(WorldModel const& world, PartyMemberFacts const& member)
    {
        if (!member.UsesMana())
            return 0.0f;
        return TensionWith(world, Live(world), member, float(member.mana));
    }

    float Points(WorldModel const& world, PartyMemberFacts const& member, float mana)
    {
        return WorthIn(world, Live(world), member, mana);
    }

    float Points(WorldModel const& world, PartyMemberFacts const& member, float mana, Fight const& fight)
    {
        return WorthIn(world, Planned(fight), member, mana);
    }
}
}
