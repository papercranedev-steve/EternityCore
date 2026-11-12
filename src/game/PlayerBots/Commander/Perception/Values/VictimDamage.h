/*
 * PartyBot Commander layer 1 (eyes): how much an enemy hurts the party member it hits, in
 * points (battlefield groups doc .kiro/docs/2026-11-09-doctrine/2026-11-13-battlefield-groups.md
 * 3.1). The doctrine compares it with what the main tank's going for it costs: a caster standing
 * back that hurts is the tank's too, one that does not is left to the damage dealers.
 *
 *   victim   in the fight: its victim (EnemyFacts::victim) when that is a party member; else
 *            (before the pull, or it hits nobody of ours) the alive party member nearest to it
 *            who is not a tank or a pet: the first one it would reach.
 *   dps      on that victim: its melee after the victim's armor (physical melee only) or its
 *            main ranged spell, whichever is more (the FightLoad::FoeOf rule, on the victim
 *            instead of the tank). Before resistances.
 *   points   Value::Points(dps x sec): `sec` is the caller's, how long it hits (until the
 *            damage dealers kill it, by the plan).
 *
 * Pure: WorldModel facts.
 */

#ifndef MANGOS_COMMANDER_VICTIM_DAMAGE_H
#define MANGOS_COMMANDER_VICTIM_DAMAGE_H

#include "Commander/Perception/WorldModel.h"

namespace Commander
{
namespace VictimDamage
{
    struct Hit
    {
        PartyMemberFacts const* victim = nullptr;   // nullptr: nobody to hit (dps, points 0)
        float dps = 0.0f;
        float points = 0.0f;
    };

    // The alive non-tank, non-pet party member nearest to `at` (nullptr = none).
    PartyMemberFacts const* NearestSoft(WorldModel const& world, Position3 const& at);

    Hit Of(WorldModel const& world, EnemyFacts const& enemy, float sec);
    // Before the pull: an idle creature (PackCreature::meleeDps / rangedDps are estimates).
    Hit Of(WorldModel const& world, PackCreature const& creature, float sec);
}
}

#endif
