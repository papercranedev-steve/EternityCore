#include "Commander/Perception/Values/ManaWorth.h"
#include "Commander/Perception/Forecast/ManaBudget.h"
#include "Commander/Perception/Forecast/WaveEvent.h"
#include "Commander/Perception/Values/ValueModel.h"
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
            return m.HealsTeam();
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
            float floor = SCARCITY_FLOOR;
            // The live fight: an event's waves still to come count too (WaveEvent::FutureMana).
            bool live = false;
        };

        Context Live(WorldModel const& world)
        {
            Context c;
            c.fightSec = Value::FightRemainingSec(world);
            c.incomingDps = world.rates.incomingDps;
            c.headroom = Headroom(world);
            c.live = true;
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
            float const future = c.live ? WaveEvent::FutureMana(world, m) : 0.0f;
            if (IsHealer(m))
            {
                float const needed = ManaBudget::NeededHealing(c.incomingDps, c.fightSec, c.headroom, world.rates.healers);
                return ManaBudget::Tension(ManaBudget::NeededMana(needed, HealPerMana(m)) + future, mana);
            }
            if (m.fillerManaPerSec <= 0.0f)
                return 0.0f;
            // Fight left / seconds the mana lasts = mana needed / mana.
            return ManaBudget::Tension(c.fightSec * m.fillerManaPerSec + future, mana);
        }

        float WorthIn(WorldModel const& world, Context const& c, PartyMemberFacts const& member, float mana)
        {
            if (mana <= 0.0f || !member.UsesMana())
                return 0.0f;
            // Scarcity after paying it: the last mana is the dearest.
            float const scarcity = std::max(c.floor,
                ManaBudget::Scarcity(TensionWith(world, c, member, std::max(0.0f, float(member.mana) - mana))));
            if (scarcity <= 0.0f)
                return 0.0f;
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

    bool Critical(WorldModel const& world, PartyMemberFacts const& member, float mana)
    {
        if (!IsHealer(member) || !member.alive || !member.UsesMana() || member.manaPct < 0.0f)
            return false;
        if (mana < float(member.maxMana) * CRITICAL_MIN_SHARE)
            return false;
        float const after = std::max(0.0f, float(member.mana) - mana);
        return TensionWith(world, Live(world), member, after) > CRITICAL_TENSION;
    }
}
}
