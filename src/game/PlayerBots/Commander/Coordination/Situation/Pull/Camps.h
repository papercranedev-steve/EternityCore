/*
 * PartyBot Commander layer 2 (situation), the pull stance: what the player wants done with the
 * camps of the open world (TeamPlan CampMemory; design .kiro/docs/2026-11-24-pull-stance.md §3.1).
 *
 * A camp: creatures of the same kinds standing near one another. A pack belongs to one when a
 * kind of it is the camp's and it stands within CAMP_YD of one of the camp's spots (newest camp
 * first). The spots grow as its packs are pulled (one per CAMP_SPOT_STEP_YD, MAX_CAMP_SPOTS):
 * the camp is found as it is walked. Dropped when it runs out, or once the leader is
 * CAMP_FORGET_YD from all its spots.
 *   a fight he started there (LeaderActivityFacts::engaged): pulled STARTED_MEMORY_MS from his last
 *   his answer to "clear them?": pulled / left alone ANSWER_MEMORY_MS
 */

#ifndef MANGOS_COMMANDER_PULL_CAMPS_H
#define MANGOS_COMMANDER_PULL_CAMPS_H

#include "Common.h"
#include "Commander/Coordination/TeamPlan.h"
#include <vector>

namespace Commander
{
    struct LeaderEngage;

    namespace Camps
    {
        float const CAMP_YD = 50.0f;
        float const CAMP_SPOT_STEP_YD = 15.0f;
        uint32 const MAX_CAMP_SPOTS = 20;
        float const CAMP_FORGET_YD = 150.0f;
        uint32 const ANSWER_MEMORY_MS = 10 * MINUTE * IN_MILLISECONDS;
        uint32 const STARTED_MEMORY_MS = 5 * MINUTE * IN_MILLISECONDS;

        // The camp `pack` belongs to: its index in `camps`, -1 = none.
        int32 Find(std::vector<CampMemory> const& camps, PackFacts const& pack);
        // `pack` of the camp was pulled: its spot is added (when new).
        void Grow(CampMemory& camp, PackFacts const& pack);
        void Forget(std::vector<CampMemory>& camps, Position3 const& leaderPos, uint32 nowMs);
        void RememberFight(std::vector<CampMemory>& camps, LeaderEngage const& fight, uint32 nowMs);
        void RememberAnswer(std::vector<CampMemory>& camps, std::vector<uint32> const& entries, Position3 const& where,
                            bool clear, uint32 nowMs);
        std::vector<uint32> EntriesOf(PackFacts const& pack);
    }
}

#endif
