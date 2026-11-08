/*
 * PartyBot Commander layer 1 (eyes): what it costs when an enemy turns from its holder to
 * `who` now (the next Blizzard tick pulls it), counting how it gets taken back. Consequences
 * only; AreaDamageBehavior (cerebellum) decides whether a channel starts or goes on
 * (user 2026-11-08, strat-20261108-133706: a Blizzard was cancelled after one tick because a
 * Skeletal Berserker at 150 health, dead by the next tick, would have turned; and a slowed
 * melee enemy turning is cheap: the tank taunts it back before it gets there).
 *
 *   arrival   a melee enemy hits `who` only once it got there: (distance - its bounding
 *             radius - MELEE_REACH) / `speed` (the caller's live speed, slows included). An
 *             enemy standing back casting (melee off, or ranged damage measured) hits at once.
 *   back      seconds until it is off `who` again, the best of:
 *             - its holder (a tank) wins it back with threat: walk to it + `gap` / its
 *               measured threat rate (LooseEnemy::TankRate), no extra cost;
 *             - a ready taunt of the holder in sight: walk into its range + the taunt's hold
 *               (RescueValue::TauntSecOnVictim), cost as RescueValue::Taunt: LostAction(GCD +
 *               walk) + the cooldown reserve (+ a stance swap);
 *             a holder that is not a tank: neither, the enemy stays on `who`.
 *   cost      LooseEnemy::Loss(enemy, who, holder, hits) with hits = back - arrival (>= 0),
 *             plus the taunt's own cost when that is the best way; everything over at most
 *             HORIZON_SEC and the enemy's time to die.
 * `gap`: threat the holder lacks on it once `who` has it (the caller's tick prediction).
 * Pure: WorldModel facts.
 */
#ifndef MANGOS_COMMANDER_AGGRO_TURN_COST_H
#define MANGOS_COMMANDER_AGGRO_TURN_COST_H

#include "Commander/Perception/WorldModel.h"

namespace Commander
{
    namespace AggroTurnCost
    {
        static constexpr float HORIZON_SEC = 10.0f;        // = LooseEnemy::HORIZON_SEC
        static constexpr float MELEE_REACH = 5.0f;
        static constexpr float RUN_YARDS_PER_SEC = 7.0f;

        // Points lost when `enemy` turns from `holder` (its victim now) to `who`.
        float Of(WorldModel const& world, EnemyFacts const& enemy, PartyMemberFacts const& who,
                 PartyMemberFacts const& holder, float speed, float gap);
    }
}

#endif
