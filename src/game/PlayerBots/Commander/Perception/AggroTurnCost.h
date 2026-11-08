/*
 * PartyBot Commander layer 1 (eyes): what it costs when enemies turn from their holders to
 * `who` now (the next Blizzard tick pulls them), counting how they get taken back. Consequences
 * only; the area phase (Coordination/AreaPhase.h) and AreaDamageBehavior read it through
 * Perception/AreaValue.h (user 2026-11-08, strat-20261108-133706: a Blizzard was cancelled
 * after one tick because a Skeletal Berserker at 150 health, dead by the next tick, would have
 * turned; and a slowed melee enemy turning is cheap: the tank taunts it back before it gets there).
 *
 * Per enemy:
 *   arrival   a melee enemy hits `who` only once it got there: (distance - its bounding
 *             radius - MELEE_REACH) / `speed` (the caller's speed, slows included). An enemy
 *             standing back casting (melee off, or ranged damage measured) hits at once.
 *   back      seconds until it is off `who` again, for the enemies of one holder together
 *             (since 2026-11-08; before, each enemy was counted as if the tank spent its whole
 *             threat rate and its one Taunt on it alone, strat-20261108-141921):
 *             - a tank holder wins them back with threat one after another: walk to it + its
 *               turn in LooseEnemy::SequentialRecovery (the gaps at the tank's measured rate);
 *             - each ready taunt of that tank goes to one of them (in sight, not taunt immune),
 *               the one where the whole result is best: walk into range +
 *               RescueValue::TauntSecOnVictim, cost as RescueValue::Taunt (LostAction(GCD +
 *               walk) + the cooldown reserve + a stance swap); a taunt that saves nothing is not used;
 *             - a holder that is not a tank, or a tank the plan sends elsewhere (Options::
 *               busyTanks: walking off a patrol's path), neither: it stays on `who` until it dies
 *               or the horizon.
 *   hits      Points(its hits on `who` beyond what its holder took, from arrival to back) +
 *             Value::HarassLoss (a chased caster or healer does less), at most HORIZON_SEC and
 *             the enemy's time to die.
 * Together (since 2026-11-08, strat-20261108-161025: seven enemies turned to a mage, each
 * counted against "the hits on him now + this one", so none of them could kill him): `who`'s
 * health + absorbs (+ Options::extraAbsorb, a shield put up first) against the hits already on
 * it plus every turned enemy from its arrival to its back, the healer bots' heals
 * (SurvivalValue::HealOn, the first after its cast) -> time to die; death =
 * Value::DeathPenalty(who) x (SurvivalValue::Risk(with them) - Risk(without them)).
 * `gap`: threat the holder lacks on it once `who` has it (the caller's tick prediction).
 * Pure: WorldModel facts.
 */
#ifndef MANGOS_COMMANDER_AGGRO_TURN_COST_H
#define MANGOS_COMMANDER_AGGRO_TURN_COST_H

#include "Commander/Perception/WorldModel.h"
#include <vector>

namespace Commander
{
    namespace AggroTurnCost
    {
        static constexpr float HORIZON_SEC = 10.0f;        // = LooseEnemy::HORIZON_SEC
        static constexpr float MELEE_REACH = 5.0f;
        static constexpr float RUN_YARDS_PER_SEC = 7.0f;
        static constexpr float STEP_SEC = 0.25f;           // health walk of the death check

        // One enemy turning from `holder` (its victim now) to the caller's `who`.
        struct Turn
        {
            EnemyFacts const* enemy = nullptr;
            PartyMemberFacts const* holder = nullptr;
            float speed = 0.0f;
            float gap = 0.0f;
        };

        struct Options
        {
            float extraAbsorb = 0.0f;               // a shield `who` puts up first
            Position3 const* at = nullptr;          // `who` stands there instead (a spot it walks to)
            std::vector<ObjectGuid> busyTanks;      // tanks the plan sends elsewhere: no threat, no taunts
            bool noTaunts = false;                  // the tanks' taunts are kept for something else
        };

        // Points lost when all `turns` turn to `who` at once.
        float OfGroup(WorldModel const& world, std::vector<Turn> const& turns, PartyMemberFacts const& who,
                      Options const& options = Options());
    }
}

#endif
