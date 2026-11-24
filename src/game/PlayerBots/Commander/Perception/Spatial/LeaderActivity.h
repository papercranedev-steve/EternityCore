/*
 * PartyBot Commander layer 1 (eyes): what the leader does outside fights (the pull stance
 * reads it, Coordination/Situation/Pull/AutoClear.h; design .kiro/docs/2026-11-24-pull-stance.md).
 *
 * Per refresh, from the facts (after the pack refresh):
 *   comesAnyway  every pack creature (PackCreature::comesAnyway): hostile, starts fights, not
 *                stealthed, and a live member stands within its aggro range + FIGHT_MARGIN_YD.
 *   wanders      the leader's trail (one point per TRAIL_STEP_MS) covers WANDER_MS and all of it
 *                is within WANDER_YD of where he is: he stays around here, standing still too.
 *   engaged      a fight he started himself: an idle creature he has selected that would not come
 *                on its own is watched (watching stops once it would come, or he selects another
 *                idle one); it then turns up among the enemies while he attacks it or it attacks
 *                him. Selecting alone is no sign (players select to look at a level). Whether our
 *                own pull started it is the brain's to tell (TeamPlan::pull).
 * Facts only, no decision. Pure on the facts; state across refreshes, one per PartyCommander.
 */

#ifndef MANGOS_COMMANDER_LEADER_ACTIVITY_H
#define MANGOS_COMMANDER_LEADER_ACTIVITY_H

#include "Commander/Perception/WorldModel.h"
#include <vector>

namespace Commander
{
    class LeaderActivity
    {
    public:
        static constexpr float FIGHT_MARGIN_YD = 3.0f;
        static constexpr uint32 TRAIL_STEP_MS = 1000;
        static constexpr uint32 WANDER_MS = 15 * 1000;
        static constexpr float WANDER_YD = 30.0f;

        void Annotate(WorldModel& world, uint32 nowMs);

    private:
        struct Sample
        {
            uint32 atMs = 0;
            Position3 pos;
        };
        void Watch(WorldModel& world, PartyMemberFacts const& leader);

        std::vector<Sample> m_trail;
        LeaderEngage m_watched;     // creature empty = nothing watched
    };
}

#endif
