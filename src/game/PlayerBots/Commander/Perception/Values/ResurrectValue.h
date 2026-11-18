/*
 * PartyBot Commander layer 1 (eyes): what a resurrection in combat (PartyMemberFacts::combatRes:
 * Rebirth) on a dead member is worth, in points (user 2026-11-15: a healer or a tank; cooldown
 * doc .kiro/docs/2026-11-14-cooldown-spells.md 10). Consequences only; ResurrectAssigner picks.
 * After the fight every resurrection is free (out of combat, TeamBotAI), so only the rest of
 * this fight counts:
 *   back = Value::FightRemainingSec - cast time - ACCEPT_SEC (players answer the request;
 *          bots at once); nothing when back <= MIN_BACK_SEC
 *   healer  per second Points(worthDps) / (alive healers + 1): its share of the healing
 *   tank    per second Points(worthDps) x TANK_SHARE: the hits go back from soft targets to
 *           the armor
 *   other roles: nothing (not offered)
 *   benefit = per second x back
 *   cost    = LostAction(caster, max(cast, GCD)) + ManaWorth(mana) + Value::CooldownPoints
 *             (cooldown, fight left + SurvivalValue::NEXT_FIGHTS_SEC)
 * Untested: the risen member's low health and mana are not counted. Pure: WorldModel facts.
 */

#ifndef MANGOS_COMMANDER_RESURRECT_VALUE_H
#define MANGOS_COMMANDER_RESURRECT_VALUE_H

#include "Commander/Perception/Values/RescueValue.h"
#include "Commander/Perception/WorldModel.h"

namespace Commander
{
    namespace ResurrectValue
    {
        // Untested guesses.
        static constexpr float ACCEPT_SEC = 5.0f;
        static constexpr float MIN_BACK_SEC = 10.0f;
        static constexpr float TANK_SHARE = 0.5f;

        RescueValue::Option Of(WorldModel const& world, PartyMemberFacts const& caster, CombatResAbility const& a,
                               CorpseFacts const& dead);
    }
}

#endif
