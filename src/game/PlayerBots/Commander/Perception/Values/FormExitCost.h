/*
 * PartyBot Commander layer 1 (eyes): what a bot leaving its shapeshift form for one spell
 * costs (TeamBotAI::NeedsFormExit; the druid tank's Innervate from bear form, user
 * 2026-11-25: the tank and the healer matter more than a damage dealer taking a hit or two).
 * Consequences only; BoostValue adds it to the spell's cost.
 *
 * Timeline: the form goes (instant, rage or energy lost), the spell (its global cooldown
 * `outSec`, the caster's armor without the form), the form back (FORM_BACK_SEC more global
 * cooldown); a bear then has no rage until it hits and is hit again (RAGE_REBUILD_SEC).
 *   idle      threat-less time: outSec + RAGE_REBUILD_SEC for a tank form (tankStance), outSec else
 *   action    Value::LostAction(caster, FORM_BACK_SEC (+ RAGE_REBUILD_SEC in a tank form));
 *             the spell's own global cooldown is the caller's
 *   armor     the enemies on it hit the caster's armor without the form for outSec: armor /
 *             StandIn::BEAR_ARMOR_MULT (DIRE_BEAR_ARMOR_MULT from DIRE_BEAR_LEVEL), the physical
 *             share of its danger (SurvivalValue::Danger::physicalDps) x the extra it lets
 *             through -> Points
 *   turns     every enemy on it whose threat runway with the caster idle (ThreatRunway::Idle)
 *             is shorter than idle turns to its taker: AggroTurnCost::OfGroup per taker (the
 *             tank wins them back and taunts as usual; the taker's death counts its
 *             DeathPenalty with the heals it would get, little when the healer is dry)
 * Not allowed (no option): the caster in danger now (SurvivalValue::Danger::InDanger), or the
 * hits on it over idle, with the armor above and no heals (the healer is dry), leave it below
 * MIN_HEALTH_PCT.
 * Not counted: the rage it had (spent before it matters: RAGE_REBUILD_SEC), Furor, an enemy
 * polymorph while out of the form, a taunt delayed while out of the form.
 * Pure: WorldModel facts.
 */

#ifndef MANGOS_COMMANDER_FORM_EXIT_COST_H
#define MANGOS_COMMANDER_FORM_EXIT_COST_H

#include "Commander/Perception/WorldModel.h"

namespace Commander
{
    namespace FormExitCost
    {
        // Untested guesses.
        static constexpr float FORM_BACK_SEC = 1.5f;
        static constexpr float RAGE_REBUILD_SEC = 3.0f;
        static constexpr float MIN_HEALTH_PCT = 20.0f;

        struct Result
        {
            bool allowed = false;
            float cost = 0.0f;
        };

        Result Of(WorldModel const& world, PartyMemberFacts const& caster, float outSec);
    }
}

#endif
