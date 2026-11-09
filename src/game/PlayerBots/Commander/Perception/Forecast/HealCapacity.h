/*
 * PartyBot Commander layer 1: how much a healer bot can heal (user, 2026-11-03: the
 * healer's gear has to show in it, so the numbers come from its real spells).
 *
 * Every direct single-target heal the bot knows (all ranks, HealSpells::Estimate: +healing
 * gear, coefficients, talents, the level penalty of low ranks) is valued on the bot itself:
 *   hps      average heal x (1 + CRIT_BONUS x spell crit chance of its school)
 *            / max(cast time, GCD): chain casting that heal.
 *   perMana  the same heal / its mana cost.
 * Periodic heals and area heals are left out (a chain of direct heals is the steady case).
 *
 * Rates: map thread (live player, WorldModelBuilder fills PartyMemberFacts::healRates).
 * MaxHps: pure.
 */

#ifndef MANGOS_COMMANDER_HEAL_CAPACITY_H
#define MANGOS_COMMANDER_HEAL_CAPACITY_H

#include "Commander/Perception/WorldModel.h"
#include <vector>

class PartyBotAI;

namespace Commander
{
namespace HealCapacity
{
    float const CRIT_BONUS = 0.5f;      // a critical heal heals 150%

    std::vector<HealRate> Rates(PartyBotAI const* bot);

    // Highest hps of the rates, 0 = none.
    float MaxHps(std::vector<HealRate> const& rates);
}
}

#endif
