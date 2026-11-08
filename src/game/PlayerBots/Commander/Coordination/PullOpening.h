/*
 * PartyBot Commander layer 2: the healing hold of a line of sight pull (PullPlan::holdHealing).
 * Pure, called by PullAssigner during Settle once the arrival wait of a ranged pull fought in
 * place is over. PullBehavior holds the out of combat healers.
 *
 * Healing (holdHealing): held while a line of sight pull is Active (PullPlan::losBreak: its
 *   casters have not come to the tank yet), at most HOLD_MAX_MS after Settle's own clock
 *   started. Once they stand with the pack, a heal that turns them puts them next to the
 *   tank, which takes them back. A healer bot still heals someone below
 *   PullBehavior::HOLD_HEAL_BELOW_PCT (PullBehavior).
 *
 * Until 2026-11-08 this also held the damage dealers until the tank had threat > 0 on every
 * pulled enemy next to it, at most 4 s after the first one reached it. Nothing made the tank
 * build that threat: strat-20261108-200046, no shout, the hold ran out, five of six enemies
 * at 0 threat. That wait is the threat opening now (Coordination/ThreatOpening.h): enough
 * threat to keep each enemy through the next heal, for new enemies mid-fight too, no fixed
 * timeout.
 */

#ifndef MANGOS_COMMANDER_PULL_OPENING_H
#define MANGOS_COMMANDER_PULL_OPENING_H

#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/WorldModel.h"

namespace Commander
{
    namespace PullOpening
    {
        uint32 const HOLD_MAX_MS = 15 * 1000;

        // Sets pull.holdHealing. HOLD_MAX_MS counts from pull.phaseSinceMs (Settle's own
        // clock: reset when the arrival wait ends).
        void Update(WorldModel const& world, PullPlan& pull, uint32 nowMs);

        // Hold off (pull ended or not in Settle).
        void Release(PullPlan& pull);
    }
}

#endif
