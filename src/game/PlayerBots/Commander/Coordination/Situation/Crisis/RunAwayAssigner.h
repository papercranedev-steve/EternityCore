/*
 * PartyBot Commander layer 2 (brain), situation: the player runs, the party runs (user
 * 2026-11-15: "read the player": a player running away from a fight means retreat; the bots
 * still never run on their own). Asks for a retreat (TeamPlan::retreat.requested); the map step
 * RetreatPlanner::Step starts it (outdoors a rally point, in a dungeon out through the door, or
 * says why not).
 *
 * While the party fights and no retreat runs or was asked for (and the last one that could not
 * start is RETRY_MS ago), from the eyes' LeaderRunFacts (Perception/Spatial/LeaderRun.h):
 *   left       the leader left the map in the fight (out through a dungeon door): at once.
 *   in danger  a contingency (TeamPlan::crisis: the tank or the healers gone) or
 *              RetreatAssigner::InDanger: a run of DANGER_GAIN_YD over DANGER_RUN_MS.
 *   otherwise  CALM_GAIN_YD over CALM_RUN_MS (a player repositioning, kiting back a few yards,
 *              walking to a corner does not stop the fight), and only with an enemy on him
 *              (user 2026-11-22: walking away from a fight the bots win is no retreat, they
 *              finish it and catch up).
 * A leader who tanks drags the enemies with him (his run looks the same as a pull back to the
 * party): only when he is also LEAVE_PARTY_YD from every other living member.
 * Pure.
 */

#ifndef MANGOS_COMMANDER_RUN_AWAY_ASSIGNER_H
#define MANGOS_COMMANDER_RUN_AWAY_ASSIGNER_H

#include "Commander/Coordination/IAssigner.h"

namespace Commander
{
    class RunAwayAssigner : public IAssigner
    {
    public:
        // Untested guesses.
        static constexpr float DANGER_GAIN_YD = 8.0f;
        static constexpr uint32 DANGER_RUN_MS = 1000;
        static constexpr float CALM_GAIN_YD = 25.0f;
        static constexpr uint32 CALM_RUN_MS = 3000;
        static constexpr float LEAVE_PARTY_YD = 30.0f;
        static constexpr uint32 RETRY_MS = 5000;

        // Same id as the retreat module's off switch would be confusing: its own.
        char const* GetId() const override { return "runaway"; }
        void Assign(WorldModel const& world, TeamPlan& plan) const override;
        void Clear(TeamPlan& plan) const override;
    };
}

#endif
