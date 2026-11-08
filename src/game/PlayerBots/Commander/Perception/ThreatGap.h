/*
 * PartyBot Commander layer 1 (eyes): how much threat a tank lacks on an enemy to keep it
 * through the healer's next big heal (.kiro/docs/2026-11-08-threat-modes.md, user 2026-11-08:
 * "稳一点": enough to survive a heal, not just "some threat").
 *
 * Heal threat: heal x 0.5 (paladins 0.25) x the healer's modifiers, split evenly over the
 * enemies fighting the one healed (HostileRefManager::threatAssist): the tank, so the split is
 * over the enemies with the tank on their list. PartyMemberFacts::healThreat (biggest direct
 * heal, no overheal). strat-20261108-200046: a 2295 Greater Heal = ~160 on each of seven.
 *
 * Needed on `e`: for every other party-side unit on its list, (its threat + the next heal's
 * share if it is a healer bot) / 1.1 in melee reach, / 1.3 else (ThreatView aggro factors);
 * healer bots not on the list yet count with the heal alone. Gap = needed - the tank's threat,
 * at least 1 while the tank has none (a player healer, no healer bot: "some threat" at least).
 * Enemies that ignore assist threat (feared, confused, a breakable stun) get no heal share.
 */

#ifndef MANGOS_COMMANDER_THREAT_GAP_H
#define MANGOS_COMMANDER_THREAT_GAP_H

#include "Commander/Perception/WorldModel.h"

namespace Commander
{
    namespace ThreatGap
    {
        // Threat the next heal of the party's biggest healer bot puts on `e`.
        float NextHealThreat(WorldModel const& world, EnemyFacts const& e, ObjectGuid tank);

        // Threat `tank` lacks on `e` (0 = safe), see above.
        float Of(WorldModel const& world, EnemyFacts const& e, ObjectGuid tank);

        // Threat per second the tank adds to the enemy it hits: measured (LooseEnemy::TankRate),
        // before that its damage estimate x its threat modifier (PartyMemberFacts::soloDps,
        // threatMod). At least 1.
        float FillRate(WorldModel const& world, PartyMemberFacts const& tank);

        // Points per second the party loses while its damage dealer bots (not tanks, not
        // healers) wait for the tank: their Value::ContributionPerSec.
        float WaitCostPerSec(WorldModel const& world, ObjectGuid tank);
    }
}

#endif
