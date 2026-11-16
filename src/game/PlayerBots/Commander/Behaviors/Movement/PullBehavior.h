/*
 * PartyBot Commander layer 3: executes TeamPlan::pull (step 5).
 * Design: .kiro/docs/2026-11-01-pull-planning.md
 *
 *  Tank     Approach: walk-in pull: attack the chosen target (original chase brings it there);
 *                     fought where the pack stands: then the class's opener charge
 *                     (ClassKit::OpenerCharge, warrior Charge; the tank rotation never charges).
 *                     No charging when the pack is brought back: the plan's constraint
 *                     TeamPlan::ForbidsGapCloser (the arbiter and the rotation check it).
 *                     Ranged pull (PullPlan::rangedPullSpell): walk to the shot spot the brain
 *                     picked (PullPlan::rangedSpot, Perception/Spatial/ShotSpot.h), then cast the spell
 *                     (Shoot / Throw / Faerie Fire (Feral) / Auto Shot). Closer than the
 *                     spell's minimum range: walk-in pull. On the spot without line of sight or
 *                     out of range, or not there in time: hold, never step closer, and report
 *                     (PullShotState::spotFailed); the brain gives another spot or target.
 *                     No fight RANGED_NO_COMBAT_MS after the shot: one retry a few yards
 *                     closer if that step stays out of every idle creature's aggro range
 *                     (ShotSpot::IsClear; else shoot again in place), then walk-in pull.
 *                     The shot steps are kept in PullPlan::rangedShot (TeamPlan.h).
 *                     Face pull (PullPlan::facePull): walk to PullPlan::faceSpot the same way,
 *                     then stand there facing the target and do nothing; not noticed within
 *                     FACE_WAIT_MS: walk-in pull.
 *                     Ambush of a patrol (PullPlan::ambush, a face pull on its way): walk
 *                     there and wait until it comes by; never walks in after it.
 *           After the pull ended without a fight (PullPlan::standDownTarget): out of
 *           combat, drop that target (DropTarget), else the original AI chases it.
 *                     When the opener went out (pull spell or Charge executed, standing on the
 *                     face pull spot, or inside the target's aggro range) it writes
 *                     PullShotState::openedAtMs: PullAssigner no longer calls the pull off.
 *                     A corner fight (PullPlan::corner) does not charge either: the tank walks
 *                     back to the corner afterwards.
 *           Return:   walk backwards to the fight spot facing the pack (Backpedal.h: parry, block,
 *                     swings and the rotation keep working); the enemies follow. Forward walk
 *                     only when no backwards path is found.
 *           Settle:   ranged pull fought in place (PullPlan::awaitArrival): stay at the point
 *                     the shot was fired from, facing the pack, chase held and charges vetoed,
 *                     until PullAssigner says the pack came.
 *                     Line of sight pull (PullPlan::losBreak Active): walk backwards to PullPlan::losPoint,
 *                     out of sight of the casters standing back, and stay there (movement held,
 *                     still fighting) until PullAssigner ends it.
 *                     Standing on either point, the "stay" order scores only STAY_SCORE: the
 *                     threat module's casts (Thunder Clap, Demoralizing Shout, taunts) beat it
 *                     for one tick; the last order's movement hold (BACKPEDAL_ORDER_MS) keeps
 *                     the tank on the point meanwhile.
 *  Others   Approach / Return and the wait for the pack: stay where they are while out of
 *           combat. Settle after that: healers while PullPlan::holdHealing (a line of sight
 *           pull, Coordination/Situation/Pull/PullOpening.h; damage dealers wait for the tank's threat
 *           each on its own target, DamageTargets / ThreatRoom); a healer still heals when a
 *           member is below HOLD_HEAL_BELOW_PCT (the hold stands down, the rotation heals).
 *           A healer with a hiding point of a line of sight pull (PullPlan::losHiders) walks
 *           there while the break is Active, in combat or not. Ranged damage dealers do not
 *           follow the line of sight move.
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
        // Ranged pull: on the shot spot within SPOT_ARRIVED_YD, or within SPOT_NEAR_YD and
        // standing (a path may end a little short, SafeRoute::OFF_MESH_STEP).
        static constexpr float SPOT_ARRIVED_YD = 2.0f;
        static constexpr float SPOT_NEAR_YD = 3.5f;
        // Not there after the straight walk at RUN_SPEED_YD_PER_SEC x SPOT_WALK_TIME_FACTOR
        // (path detours) + SPOT_WALK_EXTRA_MS: the spot failed (unreachable). The walk order's
        // hold uses the same factor.
        static constexpr float SPOT_WALK_TIME_FACTOR = 1.5f;
        static constexpr uint32 SPOT_WALK_EXTRA_MS = 3000;
        // Ranged pull in range with line of sight, target still out of combat this long after
        // the first shot (miss, dodge, range edge, cast refused): step RANGED_RETRY_STEP_YD
        // closer and shoot again, once. Still nothing after another RANGED_NO_COMBAT_MS: walk in
        // (before PullAssigner::RANGED_PULL_FALLBACK_MS).
        static constexpr uint32 RANGED_NO_COMBAT_MS = 3000;
        static constexpr float RANGED_RETRY_STEP_YD = 5.0f;
        // Face pull: on the spot, the target has not noticed the tank this long (it wandered
        // away, line of sight from its new place): walk in.
        static constexpr uint32 FACE_WAIT_MS = 2500;
        // Walking backwards (Return, line of sight point): each order lasts this long and is
        // proposed again every tick; ends this long after the phase ends.
        static constexpr uint32 BACKPEDAL_ORDER_MS = 1500;
        // On the point (within POINT_ARRIVED_YD): the order only keeps the tank there.
        static constexpr float POINT_ARRIVED_YD = 1.5f;
        static constexpr float STAY_SCORE = 0.5f;
        // Held healer bots still heal a member below this (the rotation's heal step runs).
        static constexpr float HOLD_HEAL_BELOW_PCT = 50.0f;

        char const* GetId() const override { return "pull"; }
        bool AppliesTo(PartyBotAI const* bot) const override;
        void Propose(PartyBotAI const* bot, WorldModel const& world,
                     TeamPlan const& plan, std::vector<Proposal>& out) const override;
    };
}

#endif
