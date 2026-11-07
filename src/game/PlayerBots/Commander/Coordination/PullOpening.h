/*
 * PartyBot Commander layer 2: who may start fighting after a pull (PullPlan::holdDamage,
 * PullPlan::holdHealing). Pure, called by PullAssigner during Settle once the arrival wait of
 * a ranged pull fought in place is over. PullBehavior holds the out of combat bots.
 *
 * Why (strat-20261107-143623): the tank only had threat on the creature it shot; the six that
 * came along had it on their threat list at 0. The priest's first heal (926) put 47 threat on
 * every one of them and all six turned to the priest at once, the two Skeletal Guardians
 * included; hiding the tank from them did not matter any more and they walked to the priest.
 *
 * Damage (holdDamage): the tank first has threat on every pulled enemy next to it.
 *   Next to it = acting, not crowd controlled, its victim is the tank, within THREAT_REACH of
 *   the tank (from the enemy's edge). Enemies further away (casters standing back, enemies
 *   still walking in) are not waited for here.
 *   Has threat = the tank's own threat on it is above 0 (one Thunder Clap or Demoralizing
 *   Shout is enough: either is more than a heal adds to each of several enemies).
 *   Released when that holds; when nobody is coming to the tank with melee on and nobody is
 *   next to it (nothing to wait for); THREAT_TIMEOUT_MS after the first enemy reached the tank
 *   (no rage, no area ability); or HOLD_MAX_MS after Settle's own clock started.
 * Healing (holdHealing): also held while damage is, and while a line of sight pull is Active
 *   (PullPlan::losBreak: its casters have not come to the tank yet). Once they stand with the
 *   pack, a heal that turns them puts them next to the tank, which takes them back.
 *   Released with the break (casters came, turned to someone else, timeout).
 *   A healer bot still heals someone below PullBehavior::HOLD_HEAL_BELOW_PCT (PullBehavior).
 */

#ifndef MANGOS_COMMANDER_PULL_OPENING_H
#define MANGOS_COMMANDER_PULL_OPENING_H

#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/WorldModel.h"

namespace Commander
{
    namespace PullOpening
    {
        float const THREAT_REACH = 8.0f;
        uint32 const THREAT_TIMEOUT_MS = 4 * 1000;
        uint32 const HOLD_MAX_MS = 15 * 1000;

        // Sets pull.holdDamage / holdHealing (and threatSinceMs). HOLD_MAX_MS counts from
        // pull.phaseSinceMs (Settle's own clock: reset when the arrival wait ends).
        void Update(WorldModel const& world, PullPlan& pull, Position3 const& tankAt, uint32 nowMs);

        // Holds off, threat clock reset (pull ended or not in Settle).
        void Release(PullPlan& pull);
    }
}

#endif
