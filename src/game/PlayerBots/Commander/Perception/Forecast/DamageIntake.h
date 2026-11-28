/*
 * PartyBot Commander layer 1: damage players and their pets actually take (healer planning,
 * Behaviors/Support/HealPlanBehavior.h). Value::IncomingOn only predicts enemy melee and the main ranged
 * spell; boss abilities (Crowd Pummel, Mighty Blow, area spells) are only seen here.
 *
 * Unit::DealDamage calls OnDamage with the damage after absorbs, before it is applied. Hits are
 * kept KEEP_MS per victim:
 *   dps    damage of the last RATE_WINDOW_MS / RATE_WINDOW_MS
 *   burst  the most damage within any BURST_WINDOW_MS of the last KEEP_MS (spike size)
 * WorldModelBuilder copies both into PartyMemberFacts (takenDps, takenBurst).
 *
 * Any map thread writes, the victim's map thread reads: one lock for the table. Only players and
 * player pets are logged, and only while the Commander is enabled.
 */

#ifndef MANGOS_COMMANDER_DAMAGE_INTAKE_H
#define MANGOS_COMMANDER_DAMAGE_INTAKE_H

#include "Common.h"
#include "ObjectGuid.h"

class Unit;

namespace Commander
{
namespace DamageIntake
{
    static constexpr uint32 KEEP_MS = 20000;
    static constexpr uint32 RATE_WINDOW_MS = 5000;
    static constexpr uint32 BURST_WINDOW_MS = 2000;

    struct Stats
    {
        float dps = 0.0f;
        float burst = 0.0f;
        float hitsPerSec = 0.0f;    // hits of the last RATE_WINDOW_MS / RATE_WINDOW_MS (cast pushback)
    };

    // Unit::DealDamage hook (any map thread).
    void OnDamage(Unit const* victim, uint32 damage);

    Stats Query(ObjectGuid victim, uint32 nowMs);
}
}

#endif
