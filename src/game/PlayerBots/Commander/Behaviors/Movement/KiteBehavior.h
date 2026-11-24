/*
 * PartyBot Commander layer 3: a bot whose attack has a dead zone (hunter Auto Shot, 8 yd:
 * ClassKit::MinAttackRange) gets a melee enemy on it slowed and runs out of the dead zone
 * ("kite" module; user 2026-11-15: else the enemy stays in the hunter's face for good).
 *
 * Chaser: an enemy that acts (or is only stunned), has melee on, hits this bot, and stands
 * inside the dead zone (edge distance < MinAttackRange). The nearest one is handled.
 *
 * Per tick, one of:
 *  1. Run: the chaser is held (root, stun) or slow enough (speed <= MAX_SPEED_RATIO x the
 *     bot's run speed). Destination: RangedSpot::Find around the chaser, the search the ranged
 *     stance uses (position field cells: no idle pack, no patrol before the fight ends, no
 *     hazard, exposure to the other enemies and sight margin in the loss, line of sight to the
 *     chaser and a safe path checked), cells Gap()..Gap() + BAND_YD from it, kept only when
 *     the bot gets there ARRIVE_MARGIN beyond its dead zone while the chaser follows at its
 *     speed (the cell's walk estimate), and on the bot's side of the chaser. No running to a
 *     party member: the bot keeps the enemy and shoots it. MoveTo, tier Assigned.
 *       benefit = Value::HitLoss + Value::HarassLoss over the free seconds: the walk plus the
 *                 time the chaser needs from the arrival gap back to melee, at most FREE_CAP_SEC
 *                 and the chaser's time to die
 *       cost    = Value::LostAction(bot, walk) + the spot's exposure and sight margin
 *  2. Slow: a kit slow (ClassKit::KiteSlows, Wing Clip) castable on the chaser that would make
 *     it slow enough (the spell's SPELL_AURA_MOD_DECREASE_SPEED, the chaser not immune). Cast,
 *     tier Assigned; benefit = the run's benefit at the slowed speed, cost = LostAction(GCD) +
 *     the run's walk.
 *  3. Pet stun: the slow does not take and the pet fights the chaser: ClassKit::PetStun
 *     (Intimidation) on the pet, its next hit stuns the chaser; valued like 2 with the chaser
 *     held. Not on a chaser the doctrine stamped Swarm or SelfDestruct (no control on them,
 *     as in CrowdControlAssigner).
 * While its own run is under way: Hold (score 0) so the rotation's first cast does not stop
 * the walk.
 *
 * The other modules: EscapeBehavior runs to a tank farther than its START_DISTANCE (8 yd); both
 * propose at tier Assigned and the arbiter keeps the better (in danger the escape is Urgent).
 * Next to the tank with the enemy still on the bot (the tank did not take it) this module is
 * what is left. Once the enemy is on someone else, RangedStanceBehavior keeps the bot out of
 * the dead zone (its cells start beyond MinAttackRange). The rotation casts Concussive Shot
 * on a victim running at the hunter.
 *
 * Not while: the retreat runs, the pull is in Approach / Return, out of combat, rooted, cannot act.
 * Known limits: one chaser at a time (a second one in melee stays where it is); the chaser's
 * slow may run out during the walk.
 */

#ifndef MANGOS_COMMANDER_KITE_BEHAVIOR_H
#define MANGOS_COMMANDER_KITE_BEHAVIOR_H

#include "Commander/Behaviors/IBehavior.h"

namespace Commander
{
    class KiteBehavior : public IBehavior
    {
    public:
        // Run only from a chaser at most this fraction of the bot's run speed (a 50% slow).
        static constexpr float MAX_SPEED_RATIO = 0.75f;
        // Arrive this far beyond the dead zone (edge distance).
        static constexpr float ARRIVE_MARGIN = 3.0f;
        // Melee reach of a chaser (edge distance) for the free seconds after the arrival.
        static constexpr float MELEE_GAP = 2.0f;
        static constexpr float FREE_CAP_SEC = 6.0f;
        // Cells this far beyond the nearest gap that works (more walk, little more time).
        static constexpr float BAND_YD = 8.0f;
        // New sight checks per tick for the run's spot (the ranged stance has its own).
        static constexpr uint32 MAX_SIGHT_CHECKS = 24;
        static constexpr float PATH_FACTOR = 1.2f;
        static constexpr uint32 HOLD_EXTRA_MS = 500;

        char const* GetId() const override { return "kite"; }
        bool AppliesTo(TeamBotAI const* bot) const override;
        void Propose(TeamBotAI const* bot, WorldModel const& world,
                     TeamPlan const& plan, std::vector<Proposal>& out) const override;
    };
}

#endif
