#include "Commander/Perception/Values/ResurrectValue.h"
#include "Commander/Perception/Values/ManaWorth.h"
#include "Commander/Perception/Values/SurvivalValue.h"
#include "Commander/Perception/Values/ValueModel.h"
#include <algorithm>

namespace Commander
{
namespace ResurrectValue
{
    RescueValue::Option Of(WorldModel const& world, PartyMemberFacts const& caster, CombatResAbility const& a,
                           CorpseFacts const& dead)
    {
        RescueValue::Option o;
        float const castSec = a.castMs / 1000.0f;
        float const back = Value::FightRemainingSec(world) - castSec - (dead.isBot ? 0.0f : ACCEPT_SEC);
        if (back <= MIN_BACK_SEC)
            return o;

        float perSec = 0.0f;
        if (dead.role == ROLE_HEALER)
        {
            uint32 healers = 0;
            for (PartyMemberFacts const& m : world.party)
                if (!m.isPet && m.alive && m.role == ROLE_HEALER)
                    ++healers;
            perSec = Value::Points(world, world.rates.worthDps) / float(healers + 1);
        }
        else if (dead.role == ROLE_TANK)
            perSec = Value::Points(world, world.rates.worthDps) * TANK_SHARE;
        o.benefit = perSec * back;
        if (o.benefit <= 0.0f)
            return o;

        o.cost = Value::LostAction(world, caster, std::max(castSec, Value::GCD_SEC)) +
                 ManaWorth::Points(world, caster, float(a.manaCost)) +
                 Value::CooldownPoints(a.cooldownMs / 1000.0f, Value::FightRemainingSec(world) + SurvivalValue::NEXT_FIGHTS_SEC);
        return o;
    }
}
}
