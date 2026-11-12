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
}
}

#endif
