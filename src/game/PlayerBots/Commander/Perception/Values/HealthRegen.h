/*
 * PartyBot Commander layer 1 (eyes): how fast a unit gets its health back by itself out of
 * combat (user 2026-11-22: healers topped up every scratch out of combat, also at levels 1-5
 * where natural regeneration fills a small gap in seconds). Same rules as
 * Player::RegenerateHealth, which runs every REGEN_TIME_PLAYER_FULL (2 s):
 *   natural  spirit (Unit::GetRegenHPPerSpirit) x health rate config x the health regen
 *            percent auras x the bot's own trait (PlayerAI::RegenRate), x 1.5 sitting;
 *            plus SPELL_AURA_MOD_HEALTH_REGEN_IN_COMBAT (per 5 s)
 *   food     SPELL_AURA_MOD_REGEN while it lasts (its remaining duration)
 * Players only; any other unit (a pet) counts as not regenerating (NO_LIMIT).
 */

#ifndef MANGOS_COMMANDER_HEALTH_REGEN_H
#define MANGOS_COMMANDER_HEALTH_REGEN_H

#include "Common.h"

class Unit;

namespace Commander
{
namespace HealthRegen
{
    static constexpr float NO_LIMIT = 1.0e9f;

    struct Rate
    {
        float naturalPerSec = 0.0f;
        float foodPerSec = 0.0f;
        float foodSec = 0.0f;       // food left (seconds); 0 = not eating
    };

    // Out of combat rates now (a unit in combat: zero, as the game gives it none).
    Rate OutOfCombat(Unit const* unit);

    // Seconds until `unit` is at full health by itself out of combat; NO_LIMIT when it does not
    // get there (no regeneration known).
    float SecondsToFull(Unit const* unit);
}
}

#endif
