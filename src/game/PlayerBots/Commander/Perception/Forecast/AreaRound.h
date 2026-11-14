/*
 * PartyBot Commander layer 1 (eyes): can the area caster chased by enemies it has under control
 * (Frost Nova roots, Cone of Cold / Frostbite slows) cast its next ground channel instead of
 * running to the tank (user 2026-11-14, doc .kiro/docs/2026-11-14-area-round.md)? Facts and a
 * worst-case forecast, no choice: AreaRoomBehavior decides.
 *
 * Request: the caster, the ground channel the area phase plans for it (spell, where it casts
 * from, the centre), and when its next self rescue is ready (Frost Nova, Blink or Cone of Cold:
 * seconds, < 0 none; read by the caller) with that rescue's mana.
 * Chasers: acting, not crowd controlled, health left, their victim is the caster.
 * Not ok when (`why`):
 *   - no chaser, no spell, or the spell is not ready;
 *   - the mana does not cover the spell and the rescue's mana;
 *   - a chaser hits from where it stands (melee off, or ranged damage): moving away does not help;
 *   - a chaser is neither rooted nor slowed (not under control);
 *   - a chaser is immune to slows (CreatureProfile::mechanicImmuneMask, MECHANIC_SNARE);
 *   - the first chaser in melee reach of `from` comes before `need` + REACT_SEC.
 * Arrival, worst case: a root breaks at the spell's first tick (damage breaks Frost Nova), then
 * the enemy walks slowed only inside the circle and AreaValue::SLOW_LINGER_SEC after
 * (AreaValue::WalkSecUnder); a slow on it now may end at once (an unknown aura time: Chilled
 * from the last Blizzard lasts 1.5 s), so a slowed one walks at full speed (at least
 * AggroTurnCost::RUN_YARDS_PER_SEC) until the first tick, as Runners::ReachSec.
 * need = the whole channel, or sooner the rescue's ready time (it handles the first one then).
 * Nobody hits the caster during that time by construction (ranged chasers are out), so no
 * separate health check.
 * Pure: WorldModel facts.
 */

#ifndef MANGOS_COMMANDER_AREA_ROUND_H
#define MANGOS_COMMANDER_AREA_ROUND_H

#include "Commander/Perception/WorldModel.h"

namespace Commander
{
namespace AreaRound
{
    static constexpr float REACT_SEC = 0.5f;    // a bot tick and a refresh after the need

    struct Request
    {
        PartyMemberFacts const* caster = nullptr;
        AreaSpellFacts const* spell = nullptr;
        Position3 from;
        Position3 centre;
        float rescueInSec = -1.0f;
        float rescueMana = 0.0f;
    };

    struct Result
    {
        bool ok = false;
        char const* why = nullptr;
        uint32 chasers = 0;
        float firstArriveSec = 0.0f;
        float needSec = 0.0f;
    };

    Result Of(WorldModel const& world, Request const& r);
}
}

#endif
