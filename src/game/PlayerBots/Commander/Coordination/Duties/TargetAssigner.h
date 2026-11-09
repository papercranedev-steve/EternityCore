/*
 * PartyBot Commander layer 2 (brain): which enemy every bot attacks (TeamPlan::targets) and the
 * party focus (TeamPlan::focus). Executed by Behaviors/Combat/TargetBehavior. Replaces the original
 * PartyBot target selection, which is deleted (user, 2026-11-07).
 * Design: .kiro/docs/2026-11-07/2026-11-07-target-assignment.md
 *
 * Duties stage, runs after the allocation and reads its results (plan.duties): no candidates of
 * its own, a target is not exclusive (five bots may share one). Steps of Apply:
 *  1. end player orders whose enemy is gone (ORDER_GRACE_MS after the order) / fight is over;
 *  2. party focus (ChooseFocus): Smith's rule from FocusAssigner::Evaluate, sticky at
 *     FocusAssigner::STICKY_SHARE, never an intercepted enemy, one about to be controlled
 *     (a CrowdControl primary this allocation), one in a patrol's path, a fresh enemy
 *     (FocusAssigner::IsEligible). Doctrine Area tactic (Coordination/Doctrine/TacticalDoctrine.h,
 *     since 2026-11-09): only Outlier enemies while one is eligible (the pile is the area
 *     spells'; user: "远程的法师由其他dps解决"). Melee bots that cannot reach the focus keep
 *     fighting the pile (DamageTargets switch range);
 *  3. Distribute: one target per bot. Also called again when a boss module moves the focus
 *     after the assigners (EncounterHelpers::SetFocus / ClearFocus).
 *
 * Candidates: party enemies (not onAllyOnly) and WorldModel::targeted (not hit yet).
 * Hard limits (TargetRules::Hittable), no points: acting and alive; not under a control that
 *   breaks on damage (anyone's), unless ControlledEnd sent this bot to wake it; not about to get
 *   one from another bot (CrowdControl duty this allocation whose spell breaks on damage); not
 *   in a patrol's path (PatrolMovePlan::avoid) unless it is this bot's intercept; not left alone
 *   by the player (`.partybot attackstop`).
 * Nobody gets a target: healers; during a retreat; out of combat while a pull holds them
 *   (non-tanks in Approach / Return / Settle, the pull's tank in WaitReady / Approach: PullBehavior
 *   opens).
 * Per bot, first that applies:
 *   intercept (DutyKind::Intercept primary)          urgent, benefit = its points
 *   player order `.partybot attackstart` / pull     forced
 *   ControlledEnd: this bot wakes the last control  forced
 *   focus mark (PartyMemberFacts::focusMarkEnemy)   forced
 *   tank      Coordination/Duties/TankTargets.cpp
 *   others    Coordination/Duties/DamageTargets.cpp
 * Forced = Urgent tier, not valued (a must, expressed by tier). Otherwise TargetBehavior proposes
 * the switch at benefit - cost, cost = Perception/Values/SwitchCost.h.
 * Suggestion (todo 13, user 2026-11-08: turning to an enemy about to run away is advice, the bot
 * finishes what it is doing): a valued switch to an enemy whose run the focus would cut
 * (FleeFacts::FocusSaves > 0, not running yet) gets TargetDuty::suggestUntilMs = now +
 * (secToFlee - SwitchCost::Seconds - SUGGEST_MARGIN_SEC); at or below 0 it is a plain switch.
 * Also a runner is a candidate focus (EnemyFacts::HitWhileControlled).
 */

#ifndef MANGOS_COMMANDER_TARGET_ASSIGNER_H
#define MANGOS_COMMANDER_TARGET_ASSIGNER_H

#include "Commander/Coordination/IAssigner.h"

namespace Commander
{
    class TargetAssigner : public IAssigner
    {
    public:
        // An attack order whose enemy is not on any list is dropped after this long (the bots
        // need a refresh or two after the command to have it as their victim).
        static constexpr uint32 ORDER_GRACE_MS = 3000;
        // A suggested switch ends this long before the switch would come too late (header).
        static constexpr float SUGGEST_MARGIN_SEC = 1.0f;

        AssignStage GetStage() const override { return AssignStage::Duties; }
        char const* GetId() const override { return "target"; }
        void Apply(WorldModel const& world, std::vector<DutyResult> const& results, TeamPlan& plan) const override;
        void Clear(TeamPlan& plan) const override;

        // Step 3 above, from plan.focus and plan.duties as they are now.
        static void Distribute(WorldModel const& world, TeamPlan& plan);
    };
}

#endif
