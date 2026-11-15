/*
 * PartyBot Commander layer 1: damage party members and their pets actually deal to each
 * creature, per attacker (who is hitting this enemy, how hard). CombatRates only sees an
 * enemy's health drop, not who caused it; the finishing forecast (FinishForecast.h) needs
 * each source's own rate to tell whether the others kill an enemy before a bot's cast lands.
 *
 * Unit::DealDamage calls OnDamage with the damage after absorbs, before it is applied (the
 * same hook as DamageIntake). Hits are kept KEEP_MS per victim; Query gives each attacker's
 * damage of the last RATE_WINDOW_MS / RATE_WINDOW_MS. A source that started hitting less
 * than the window ago is under-counted (the safe side: its target looks less covered).
 *
 * Only player-controlled attackers (players, their pets and totems) on creatures that are not
 * player-controlled, only while the Commander is enabled. Any map thread writes, the
 * victim's map thread reads: one lock for the table.
 */

#ifndef MANGOS_COMMANDER_DAMAGE_DEALT_H
#define MANGOS_COMMANDER_DAMAGE_DEALT_H

#include "Common.h"
#include "ObjectGuid.h"
#include <vector>

class Unit;

namespace Commander
{
namespace DamageDealt
{
    static constexpr uint32 KEEP_MS = 10000;
    static constexpr uint32 RATE_WINDOW_MS = 5000;

    struct Rate
    {
        ObjectGuid attacker;
        float dps = 0.0f;
    };

    // Unit::DealDamage hook (any map thread). Periodic ticks are not logged: they go on after the
    // attacker turned away, the enemy's auras tell them (EnemyFacts::partyDots).
    void OnDamage(Unit const* attacker, Unit const* victim, uint32 damage, bool periodic);

    // Attackers that hit `victim` within RATE_WINDOW_MS, with their damage per second.
    std::vector<Rate> Query(ObjectGuid victim, uint32 nowMs);
}
}

#endif
