#include "Commander/Perception/Values/BoostValue.h"
#include "Commander/Perception/Values/BurstValue.h"
#include "Commander/Perception/Values/FormExitCost.h"
#include "Commander/Perception/Values/HealerManaValue.h"
#include "Commander/Perception/Values/ManaWorth.h"
#include "Commander/Perception/Values/ValueModel.h"
#include "Commander/Perception/Spatial/PackResolver.h"
#include "SharedDefines.h"
#include <algorithm>

namespace Commander
{
namespace BoostValue
{
    namespace
    {
        float Missing(PartyMemberFacts const& m)
        {
            return m.maxMana > m.mana ? float(m.maxMana - m.mana) : 0.0f;
        }

        // Deals spell damage or heals: what Power Infusion raises.
        bool UsesSpells(PartyMemberFacts const& m)
        {
            if (!m.maxMana || m.role == ROLE_TANK || m.role == ROLE_MELEE_DPS)
                return false;
            return m.role == ROLE_HEALER || m.classId != CLASS_HUNTER;
        }

        float Cost(WorldModel const& world, PartyMemberFacts const& caster, AllyBoostAbility const& a)
        {
            float cost = ManaWorth::Points(world, caster, float(a.manaCost));
            if (a.gcd)
                cost += Value::LostAction(world, caster, Value::GCD_SEC);
            float const cooldownSec = a.cooldownMs / 1000.0f;
            if (cooldownSec > BurstValue::SHORT_COOLDOWN_SEC)
                cost += Value::CooldownPoints(cooldownSec, Value::FightRemainingSec(world));
            return cost;
        }
    }

    RescueValue::Option Of(WorldModel const& world, PartyMemberFacts const& caster, AllyBoostAbility const& a,
                           PartyMemberFacts const& target)
    {
        RescueValue::Option o;
        if (!a.ready || !target.alive || target.isPet || target.charmed)
            return o;

        switch (a.kind)
        {
            case MANA:
            {
                if (!target.maxMana || target.manaBoosted || !HealerManaValue::Counts(target) ||
                    (target.guid != caster.guid && PackResolver::Distance(caster.pos, target.pos) > a.maxRange))
                    return o;
                float const gain = std::max(0.0f, target.manaRegen * INNERVATE_REGEN_MULT +
                                                  target.manaRegen - target.manaRegenCasting) * a.durationSec;
                if (gain <= 0.0f || Missing(target) < gain || a.durationSec <= 0.0f)
                    return o;
                o.benefit = ManaWorth::Points(world, target, gain) +
                            HealerManaValue::DeathAvoided(world, target, gain / a.durationSec, a.durationSec);
                break;
            }
            case POWER:
            {
                if (!UsesSpells(target) || target.powerBoosted || !target.inCombat ||
                    (target.guid != caster.guid && PackResolver::Distance(caster.pos, target.pos) > a.maxRange))
                    return o;
                o.benefit = Value::ContributionPerSec(world, target) * POWER_SHARE * a.durationSec;
                break;
            }
            case PARTY_MANA:
            {
                if (target.guid != caster.guid)
                    return o;
                float missing = 0.0f;
                float given = 0.0f;
                for (PartyMemberFacts const& m : world.party)
                {
                    if (m.isPet || !m.alive || !m.maxMana ||
                        PackResolver::Distance(caster.pos, m.pos) > a.radius)
                        continue;
                    float const gain = std::min(Missing(m), a.manaPerTarget);
                    missing += gain;
                    given += a.manaPerTarget;
                    o.benefit += ManaWorth::Points(world, m, gain);
                }
                if (given <= 0.0f || missing < given * PARTY_MANA_MIN_SHARE)
                {
                    o.benefit = 0.0f;
                    return o;
                }
                break;
            }
            default:
                return o;
        }
        o.cost = Cost(world, caster, a);
        if (a.leavesForm)
        {
            FormExitCost::Result const exit = FormExitCost::Of(world, caster, a.gcd ? Value::GCD_SEC : 0.0f);
            if (!exit.allowed)
            {
                o = RescueValue::Option();
                return o;
            }
            o.cost += exit.cost;
        }
        return o;
    }
}
}
