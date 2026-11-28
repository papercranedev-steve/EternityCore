/*
 * PartyBot Commander layer 2 (situation), the pull stance in a dungeon: a boss the player said
 * "wait" to (user 2026-11-28). A "wait" (等一下), or a question shown and not answered, is
 * not a "never": the boss is not asked about again until he walks CLOSER_YD nearer to it
 * (PackFacts::ApproachYd) or selects one of its pack. Kept by the team (AutoClearPlan), not by
 * the clear: a clear that ends and starts again does not ask again every RETRY_MS.
 * Forgotten once he leaves the dungeon (AutoClear).
 */

#ifndef MANGOS_COMMANDER_BOSS_WAITS_H
#define MANGOS_COMMANDER_BOSS_WAITS_H

#include "Common.h"
#include "ObjectGuid.h"
#include <vector>

namespace Commander
{
    struct AutoClearPlan;
    struct PackFacts;

    namespace BossWaits
    {
        float const CLOSER_YD = 10.0f;

        // The player's "wait" for this pack still holds. Walked CLOSER_YD nearer or `selection`
        // is one of its members: the wait is forgotten (false).
        bool Holds(AutoClearPlan& autoClear, PackFacts const& pack, ObjectGuid selection);
        // `pack`: ClearPlanner::PackKey of the boss's pack.
        void Remember(AutoClearPlan& autoClear, std::vector<ObjectGuid> const& pack, float approachYd);
        void Forget(AutoClearPlan& autoClear, std::vector<ObjectGuid> const& pack);
    }
}

#endif
