/*
 * PartyBot Commander layer 1 (eyes): the enemies running at one party member in melee and when
 * each gets there (doc .kiro/docs/2026-11-12-mage-hold.md 3.2, 3.3.1). The area phase centres a
 * ground spell ahead of the first of them (AreaCentre::AheadOf) and starts the spell only when
 * it pays before that one is on the caster.
 *
 * Runner: acting, not crowd controlled, not rooted, melee on, health left, its victim is the
 * member; gap = to its edge, nearest first. Rooted ones (Frost Nova) stand: the area phase
 * covers them as a pile (doc 3.2 case three: all of them in the area, not only the first).
 * ArriveSec: seconds until it is in melee reach (its bounding radius + AreaValue::MELEE_REACH)
 * of `at`, walking straight at its speed now. With a ground spell `spell` at `centre` starting
 * now, its first tick (after spell.periodSec) slows it while inside the area and
 * AreaValue::SLOW_LINGER_SEC after (AreaValue::WalkSecUnder); one already slowed keeps its speed.
 * Pure: WorldModel facts.
 */

#ifndef MANGOS_COMMANDER_RUNNERS_H
#define MANGOS_COMMANDER_RUNNERS_H

#include "Commander/Perception/WorldModel.h"
#include <vector>

namespace Commander
{
namespace Runners
{
    struct Runner
    {
        EnemyFacts const* facts = nullptr;
        float gapYd = 0.0f;
    };

    // Nearest first (header). `at`: where the member is (or will cast from).
    std::vector<Runner> Of(WorldModel const& world, ObjectGuid member, Position3 const& at);

    // Header. `spell` null: no area spell.
    float ArriveSec(EnemyFacts const& e, Position3 const& at, AreaSpellFacts const* spell, Position3 const& centre);

    // ArriveSec to within `reach` of `at` (centre to centre), e.g. a Frost Nova's radius.
    float ReachSec(EnemyFacts const& e, Position3 const& at, float reach, AreaSpellFacts const* spell, Position3 const& centre);

    // Where `e` is `sec` seconds from now, running at its victim (user 2026-11-29: the adds a
    // spell around the caster is aimed at are on their way to whoever they hit). It stands
    // where it is when it is not acting, crowd controlled, rooted, melee off (a caster
    // standing back), has no victim, or its victim is not a member or ally; a cast it reads
    // keeps it in place until the cast ends. Otherwise it runs straight at its speed now
    // (paths not counted) and stops CHASE_GAP_YD beyond its bounding radius from the victim.
    // `mover` (a member, empty = none) is counted at `moverAt` (where it walks to) instead
    // of where it stands. Fears and new targets are not guessed.
    static constexpr float CHASE_GAP_YD = 1.5f;     // a player's combat reach
    Position3 PosAfter(WorldModel const& world, EnemyFacts const& e, float sec, ObjectGuid mover,
                       Position3 const& moverAt);
}
}

#endif
