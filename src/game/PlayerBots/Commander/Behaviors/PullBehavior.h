/*
 * PartyBot Commander layer 3: executes TeamPlan::pull (step 5).
 * Design: .kiro/docs/2026-11-01-pull-planning.md
 *
 *  Tank     Approach: walk-in pull: attack the chosen target (original chase brings it there);
 *                     fought where the pack stands: then the class's opener charge
 *                     (ClassKit::OpenerCharge, warrior Charge; the tank rotation never charges).
 *                     No charging when the pack is brought back: VetoType::GapCloser.
 *                     Ranged pull (PullPlan::rangedPullSpell): walk to a point inside
 *                     rangedPullRange on the line to the target, step closer while there is no
 *                     line of sight, then cast the spell (Shoot / Throw / Faerie Fire (Feral) /
 *                     Auto Shot). Closer than the spell's minimum range: walk-in pull.
 *                     No fight RANGED_NO_COMBAT_MS after the shot: one retry a few yards
 *                     closer, then walk-in pull.
 *                     The shot steps are kept in PullPlan::rangedShot (TeamPlan.h).
 *           Return:   walk backwards to the fight spot facing the pack (Backpedal.h: parry, block,
 *                     swings and the rotation keep working); the enemies follow. Forward walk
 *                     only when no backwards path is found.
 *           Settle:   ranged pull fought in place (PullPlan::awaitArrival): stay at the point
 *                     the shot was fired from, facing the pack, chase held and charges vetoed,
 *                     until PullAssigner says the pack came.
 *                     Line of sight pull (PullPlan::losBreak Active): walk backwards to PullPlan::losPoint,
 *                     out of sight of the casters standing back, and stay there (movement held,
 *                     still fighting) until PullAssigner ends it.
 *  Others   Approach / Return, the wait for the pack, and the first PullAssigner::SETTLE_MS of Settle: stay where they
 *           are while out of combat. Healers and ranged never follow the line of sight move.
 *           Once in combat (healers and damage dealers alike) the original AI acts normally.
 */

#ifndef MANGOS_COMMANDER_PULL_BEHAVIOR_H
#define MANGOS_COMMANDER_PULL_BEHAVIOR_H

#include "Commander/Behaviors/IBehavior.h"

namespace Commander
{
    class PullBehavior : public IBehavior
    {
    public:
        static constexpr float RUN_SPEED_YD_PER_SEC = 7.0f;
        // Ranged pull in range without line of sight: walk this far towards the target, look again.
        static constexpr float LOS_STEP_YD = 4.0f;
        // Ranged pull in range with line of sight, target still out of combat this long after
        // the first shot (miss, dodge, range edge, cast refused): step RANGED_RETRY_STEP_YD
        // closer and shoot again, once. Still nothing after another RANGED_NO_COMBAT_MS: walk in
        // (before PullAssigner::RANGED_PULL_FALLBACK_MS).
        static constexpr uint32 RANGED_NO_COMBAT_MS = 3000;
        static constexpr float RANGED_RETRY_STEP_YD = 5.0f;
        // Walking backwards (Return, line of sight point): each order lasts this long and is
        // proposed again every tick; ends this long after the phase ends.
        static constexpr uint32 BACKPEDAL_ORDER_MS = 1500;

        char const* GetId() const override { return "pull"; }
        bool AppliesTo(PartyBotAI const* bot) const override;
        void Propose(PartyBotAI const* bot, WorldModel const& world,
                     TeamPlan const& plan, std::vector<Proposal>& out) const override;
    };
}

#endif
