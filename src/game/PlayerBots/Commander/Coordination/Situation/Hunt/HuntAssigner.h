/*
 * PartyBot Commander layer 2 (brain), situation: a team of bots with no player in it
 * (Commander/Team.h, a field population bot alone) finds its own fights, as a player levelling
 * does: the next creature to attack (TeamPlan::hunt, attacked by Behaviors/Combat/HuntBehavior).
 * A player's party never hunts: the player chooses its fights.
 *
 * Out of a fight, not pulling or retreating, everyone ready (PullPlanner::CheckReady: health,
 * mana, alive, out of combat; resting is RestBehavior's), the nearest pack of the eyes' scan
 * (WorldModel::packs) that
 *  - has at most as many creatures as the team has members, none elite, a boss, stealthed or
 *    a critter, none with somebody else's loot rights;
 *  - is at the team's level: from LEVEL_BELOW under the highest member to LEVEL_ABOVE over it;
 *  - can be walked to without coming near another idle pack (PackRoute: a way, no blocker),
 *    within MAX_APPROACH_YD.
 * The choice is kept until the fight starts, the creature no longer qualifies, or KEEP_MS.
 * Nothing found: idleSinceMs tells the bot's life (Population/) how long it has had nothing
 * to do here (time to move on). Pure.
 */

#ifndef MANGOS_COMMANDER_HUNT_ASSIGNER_H
#define MANGOS_COMMANDER_HUNT_ASSIGNER_H

#include "Commander/Coordination/IAssigner.h"

namespace Commander
{
    struct PackFacts;

    class HuntAssigner : public IAssigner
    {
    public:
        static constexpr uint32 LEVEL_BELOW = 4;
        static constexpr uint32 LEVEL_ABOVE = 1;
        static constexpr float MAX_APPROACH_YD = 45.0f;
        static constexpr uint32 KEEP_MS = 20 * 1000;

        char const* GetId() const override { return "hunt"; }
        void Assign(WorldModel const& world, TeamPlan& plan) const override;
        void Clear(TeamPlan& plan) const override;

        // `pack` is a fight this team takes on its own (the rules above, without the walk).
        static bool IsHuntable(WorldModel const& world, PackFacts const& pack);
    };
}

#endif
