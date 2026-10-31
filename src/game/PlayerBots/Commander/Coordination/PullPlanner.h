/*
 * PartyBot Commander layer 2: pull planning (step 5).
 * Design: .kiro/docs/2026-11-01-pull-planning.md
 *
 * PullPlanner::Plan  - started by `.partybot ai pull`. Reads live objects (assist prediction
 *                      uses the server's own checks), so it runs on the world thread while
 *                      maps are idle. Picks the pack member that brings the fewest extra
 *                      creatures and fills TeamPlan::pull.
 * PullAssigner       - pure; advances the pull phases on every perception refresh.
 */

#ifndef MANGOS_COMMANDER_PULL_PLANNER_H
#define MANGOS_COMMANDER_PULL_PLANNER_H

#include "Commander/Coordination/IAssigner.h"
#include "Commander/Coordination/TeamPlan.h"
#include <string>
#include <vector>

class Player;
class Creature;

namespace Commander
{
    class PartyCommander;

    namespace PullPlanner
    {
        // Leader is the player who gave the command; `selected` may be null (nearest pack).
        // Returns false with the reason in `report` when no pull can be planned.
        bool Plan(Player* leader, PartyCommander& commander, Creature* selected, bool skipReady,
                  std::vector<std::string>& report);

        float const READY_HEALTH_PCT = 90.0f;
        float const READY_MANA_PCT = 80.0f;
        float const READY_MAX_DISTANCE = 40.0f;   // from the leader

        // Pure: who is not ready. Players count too (the leader's own health matters).
        struct Issue
        {
            ObjectGuid who;
            char const* what;   // "dead", "hp", "mana", "far", "combat"
            float value;
        };
        std::vector<Issue> CheckReady(WorldModel const& world);
    }

    class PullAssigner : public IAssigner
    {
    public:
        static constexpr uint32 WAIT_READY_TIMEOUT_MS = 90 * 1000;
        static constexpr uint32 APPROACH_TIMEOUT_MS = 20 * 1000;
        static constexpr uint32 RETURN_TIMEOUT_MS = 8 * 1000;
        static constexpr uint32 SETTLE_MS = 2500;
        static constexpr float RETURN_ARRIVED = 4.0f;

        char const* GetId() const override { return "pull"; }
        void Assign(WorldModel const& world, TeamPlan& plan) const override;
        void Clear(TeamPlan& plan) const override;
    };
}

#endif
