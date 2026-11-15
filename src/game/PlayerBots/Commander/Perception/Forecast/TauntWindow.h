/*
 * PartyBot Commander layer 1 (eyes): what a taunt that copies threat gets by waiting while a
 * loose enemy runs at the ranged member it turned to (user 2026-11-15: the tank "free-rides"
 * the threat the caster still adds). Taunt and Growl (SPELL_EFFECT_ATTACK_ME,
 * Spell::EffectTaunt) give the taunter the threat of the enemy's current victim, for good, at
 * the moment they land: every hit the victim lands before that is the tank's too. A temporary
 * taunt (Mocking Blow, TauntAbility::holdSec) lends it only: nothing to gain.
 *
 * Of(e, victim, tank, taunt):
 *   arriveSec     the enemy in melee reach of the victim (Runners::ArriveSec, straight at its
 *                 speed now); until then it hits nobody. NO_LIMIT while it is rooted.
 *   reachLeftSec  the enemy stays within the taunt's reach of the tank (TauntAbility::maxRange,
 *                 from its edge): (reach - gap) / (its speed - TANK_RUN_SPEED), the tank running
 *                 after it; NO_LIMIT when the tank keeps up or it is rooted; 0 = out of reach now.
 *   threatNow     the victim's threat on it (EnemyFacts::victimThreat).
 *   threatPerSec  the victim's measured rate on it (ThreatEntry::threatPerSec).
 *   hitThreat / hitLandSec  the victim's direct hit on it not landed yet (PartyMemberFacts::
 *                 pendingHit: cast left + flight, its threat).
 * ThreatGain(w, sec): what the victim adds on it within `sec`: its rate x sec, at least its
 *   pending hit when that lands by then (the rate already averages its hits).
 * Pure: WorldModel facts.
 */

#ifndef MANGOS_COMMANDER_TAUNT_WINDOW_H
#define MANGOS_COMMANDER_TAUNT_WINDOW_H

#include "Commander/Perception/WorldModel.h"

namespace Commander
{
namespace TauntWindow
{
    float const TANK_RUN_SPEED = 7.0f;      // yards per second (= RescueValue::RUN_YARDS_PER_SEC)

    struct Window
    {
        float arriveSec = 0.0f;
        float reachLeftSec = 0.0f;
        float threatNow = 0.0f;
        float threatPerSec = 0.0f;
        float hitThreat = 0.0f;
        float hitLandSec = 0.0f;
    };

    Window Of(EnemyFacts const& e, PartyMemberFacts const& victim, PartyMemberFacts const& tank,
              TauntAbility const& taunt);

    float ThreatGain(Window const& w, float sec);
}
}

#endif
