#include "Commander/Perception/Values/HealerManaValue.h"
#include "Commander/Perception/Forecast/FightLoad.h"
#include "Commander/Perception/Tanks.h"
#include "Commander/Perception/Values/ValueModel.h"
#include "SharedDefines.h"
#include <algorithm>

namespace Commander
{
namespace HealerManaValue
{
    namespace
    {
        bool IsPlayerHealer(PartyMemberFacts const& m)
        {
            return !m.isBot && !m.isPet && m.role == ROLE_HEALER && m.CanAct() && m.UsesMana() &&
                   !m.spareHealRates.empty();
        }
    }

    bool Counts(PartyMemberFacts const& m)
    {
        return (m.HealsTeam() && m.CanAct() && !m.healRates.empty()) || IsPlayerHealer(m);
    }

    float DeathAvoided(WorldModel const& world, PartyMemberFacts const& healer, float regenPerSec, float sec)
    {
        if (regenPerSec <= 0.0f || sec <= 0.0f || !world.rates.dealtMeasured || !Counts(healer))
            return 0.0f;
        PartyMemberFacts const* holder = Tanks::HitTaker(world);
        if (!holder)
            return 0.0f;

        FightLoad::Lineup lineup;
        for (PartyMemberFacts const& m : world.party)
            if (IsPlayerHealer(m))
                lineup.spareHealers.push_back(m.guid);
        FightLoad::Input in;
        if (!FightLoad::FromWorld(world, lineup, in))
            return 0.0f;

        FightLoad::Healer* boosted = nullptr;
        for (FightLoad::Healer& h : in.healers)
        {
            if (h.guid == healer.guid)
                boosted = &h;
            if (PartyMemberFacts const* m = world.FindMember(h.guid))
                for (ManaItemAbility const& a : m->manaItems)
                    if (a.ready)
                        h.itemMana += a.mana;
        }
        if (!boosted)
            return 0.0f;

        FightLoad::Result const without = FightLoad::Simulate(in, "mana boost: without");
        float const riskWithout = FightLoad::Risk(without);
        if (riskWithout <= 0.0f)
            return 0.0f;
        boosted->boostRegenPerSec = regenPerSec;
        boosted->boostSec = sec;
        FightLoad::Result const with = FightLoad::Simulate(in, "mana boost: with");
        float const saved = riskWithout - FightLoad::Risk(with);
        if (saved <= 0.0f)
            return 0.0f;
        return saved * Value::HolderDeathPenalty(world, *holder);
    }
}
}
