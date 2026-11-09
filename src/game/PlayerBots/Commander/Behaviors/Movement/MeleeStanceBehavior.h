/*
 * PartyBot Commander layer 3: where a melee damage dealer stands while the party fights
 * ("meleestance"). Design: movement handoff doc 2026-11-07 step 3 (melee part; tanks later).
 *
 * Before: the original chase (CombatBotBaseAI::BeginChasing, MoveChase behind the victim)
 * walked any path to the victim, also past idle packs; the brain only drew a straight line
 * (RouteRisk::CrossesIdle). A melee bot without a target followed the player.
 *
 * Now the module takes the chase and the follow over (Takeover::Stance, PartyBotAI::
 * IsChaseControlled: BeginChasing stands down, UpdateAI does not follow the leader) while the
 * party fights (WorldModel::InFight), also for a bot not in combat itself.
 *
 * The enemy to stand at: the brain's target for the bot (TeamPlan::targets), else its victim.
 *  - Its walk there passes an idle creature or finds no path (RouteRisk::Blocked: the navmesh
 *    route of Perception/Spatial/MeleeRoutes.h, the straight line before it is checked), or the enemy
 *    stands in a patrol's path the party moved off (PatrolMovePlan::avoid): no chase; a chase
 *    under way is stopped (StopChase). The brain gives it another target next refresh
 *    (DamageTargets: a current target it cannot walk to counts as none).
 *  - Otherwise chase it (BotActionType::Chase: attack it if it is not the victim yet, MoveChase
 *    at the chosen angle). Angle: the victim hits this bot: 0 (in front, no circling); else
 *    the first of ANGLES (behind first, then turning to the sides) whose melee spot is outside
 *    every idle creature's reach and not where an idle patrol walks before the fight should end
 *    (PatrolAssigner::InPath); none: behind (PI). Areas are the position module's.
 *    Proposed only when the bot does not run that chase yet (PartyBotAI::GetChaseAngle) or the
 *    spot of its angle became unsafe. Value: out of reach Value::LostAction(HORIZON_SEC), in
 *    reach LostAction(KEEP_SEC) (keeps up with the victim), cost 0; tier Assigned.
 * No enemy it can walk to: wait near the tank (Tanks::Main, else the leader). Farther than
 * WAIT_NEAR_YD: walk to a point WAIT_RADII from it, nearest first, outside every idle
 * creature's reach and patrol path, ground within MAX_STEP_Z, safe path (SafeRoute::
 * PathIsSafe); value LostAction(walk seconds) as the walk saved later, cost 0. Nothing found:
 * it stays.
 *
 * Not while: the retreat runs, rooted, the bot cannot act, out of combat and told to wait by
 * the pull (PullPlan::HoldsWaiting), or a movement hold is on (a MoveTo of the position module
 * or its own walk: then Hold so the rotation does not stop the walk). The map checks run for
 * at most MAX_GEOMETRY_CHECKS points per tick.
 *
 * Known limits: the chase's own path (MoveChase follows a moving victim) is not checked
 * between refreshes; one melee does not avoid another's spot; areas (HazardZones) are left to
 * the position module, which steps melee out once per area.
 */

#ifndef MANGOS_COMMANDER_MELEE_STANCE_BEHAVIOR_H
#define MANGOS_COMMANDER_MELEE_STANCE_BEHAVIOR_H

#include "Commander/Behaviors/IBehavior.h"

namespace Commander
{
    class MeleeStanceBehavior : public IBehavior
    {
    public:
        static constexpr float MELEE_REACH = 5.0f;          // DamageTargets::MELEE_REACH
        static constexpr float SPOT_GAP = 1.0f;             // MoveChase distance
        static constexpr float WAIT_NEAR_YD = 15.0f;
        static constexpr uint32 RING_POINTS = 12;
        static constexpr uint32 MAX_GEOMETRY_CHECKS = 4;    // GetHeight + pathfinding each
        static constexpr float MAX_STEP_Z = 4.0f;
        static constexpr float MAX_PATH_DETOUR = 1.8f;
        static constexpr float HORIZON_SEC = 8.0f;
        static constexpr float KEEP_SEC = 1.0f;
        static constexpr float PATH_FACTOR = 1.2f;
        static constexpr uint32 HOLD_EXTRA_MS = 500;

        char const* GetId() const override { return "meleestance"; }
        bool AppliesTo(PartyBotAI const* bot) const override;
        bool TakesOver(Takeover what, PartyBotAI const* bot, WorldModel const& world, TeamPlan const& plan) const override;
        void Propose(PartyBotAI const* bot, WorldModel const& world,
                     TeamPlan const& plan, std::vector<Proposal>& out) const override;
    };
}

#endif
