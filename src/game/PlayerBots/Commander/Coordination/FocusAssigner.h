/*
 * PartyBot Commander layer 2: kill priority of enemies (Evaluate) and must-kill intercepts.
 * Design: .kiro/docs/2026-11-01-focus-assignment.md
 * Since 2026-11-07 the party focus is chosen by TargetAssigner (TargetAssigner.h), together with
 * every bot's target; this module offers the intercepts and provides the scores below.
 *
 * Focus (TeamPlan::focus): one enemy the damage dealers kill first. Kill order follows
 * Smith's rule (weighted shortest job first): highest harm rate / kill time first.
 *   harm rate (points/s) = Value::HitDps on its victim
 *                        + heal and mana drain spells: CastValues::Evaluate points / repeat interval
 *                        + one-off bonuses / ONE_OFF_HORIZON_SEC (about to flee, support summon,
 *                          summon that dies in a few hits)
 *   kill time (s)        = health / party damage per second (whole party on it)
 *   score                = harm rate / kill time
 * Swapping two kills i (focus) before j (current) saves w_i * t_j - w_j * t_i points, which
 * is what FocusBehavior uses as the benefit of a switch (times the bot's share of damage).
 *
 * Intercepts (TeamPlan::intercepts): enemies one bot kills at once without moving the party
 * focus (walking bombs, summons running at a healer). Combat scenarios 8 and 11.
 * Since 2026-11-03 offered to DutyAllocator (one primary duty per bot, shared with interrupts):
 * candidates are ranged damage dealers within INTERCEPT_RANGE and melee within
 * MELEE_INTERCEPT_RANGE (ranged within PATROL_INTERCEPT_RANGE for patrol casters), ranked by the
 * lowest switch cost (Perception/SwitchCost.h; the previous interceptor gets
 * INTERCEPT_STICKY_YARDS of walking off), and only if the intercept is worth more than the switch.
 * Until then: ranged first, then melee, nearest first.
 */

#ifndef MANGOS_COMMANDER_FOCUS_ASSIGNER_H
#define MANGOS_COMMANDER_FOCUS_ASSIGNER_H

#include "Commander/Coordination/IAssigner.h"
#include <string>

namespace Commander
{
    struct EnemyFacts;

    // Kill priority of one enemy (see file comment).
    struct FocusScore
    {
        float harmRate = 0.0f;      // points per second it costs us while alive
        float killSec = 0.0f;       // seconds the whole party needs to kill it
        float score = 0.0f;         // harmRate / killSec
        std::string reason;         // which parts contributed, for `.partybot ai plan`
    };

    class FocusAssigner : public IAssigner
    {
    public:
        // Keep the current focus while its score is at least this share of the best one.
        static constexpr float STICKY_SHARE = 0.7f;
        // Enemies seen for less than this are left to the tank first (same window as
        // ThreatBehavior::NEW_ENEMY_SETTLE_SEC).
        static constexpr float NEW_ENEMY_SETTLE_SEC = 3.0f;
        // One-off bonuses (points) are spread over this many seconds to become a harm rate:
        // "lost within the next ~10 s if it is not dead by then".
        static constexpr float ONE_OFF_HORIZON_SEC = 10.0f;
        // About to flee (health within FleePredictor::WINDOW_PCT above its flee line) or
        // running: what having the party on it saves, FleeFacts::FocusSaves (the whole run's
        // joiners minus a focused kill's, Perception/FleePredictor.h). Nothing to call: no
        // bonus. Since 2026-11-08; before that a flat 10 points. A runner stays eligible
        // (EnemyFacts::HitWhileControlled).
        // Summon that supports its side (totem, or has a heal spell: Healing Ward, scenario 11).
        static constexpr float SUPPORT_SUMMON_POINTS = 8.0f;
        // Summon the party kills within this many seconds ("a few hits").
        static constexpr float QUICK_KILL_SEC = 2.0f;
        static constexpr float QUICK_KILL_POINTS = 3.0f;
        // Shortest kill time used in the score, so a 1 hp add does not get an infinite score.
        static constexpr float MIN_KILL_SEC = 0.5f;

        // Intercepts: who may take one. Ranged bots within INTERCEPT_RANGE of the enemy,
        // melee bots only when the enemy is already close (no running across the room).
        static constexpr float INTERCEPT_RANGE = 40.0f;
        static constexpr float MELEE_INTERCEPT_RANGE = 10.0f;
        // Caster in a patrol's path (PatrolMovePlan::kill): ranged only, within spell range
        // (the chase into the path is blocked, they cannot walk closer).
        static constexpr float PATROL_INTERCEPT_RANGE = 30.0f;
        static constexpr float INTERCEPT_STICKY_YARDS = 5.0f;   // previous interceptor counts as this much closer

        // Switch cost reach (Perception/SwitchCost.h): spell range of a ranged bot, melee reach.
        static constexpr float RANGED_REACH = 30.0f;
        static constexpr float MELEE_REACH = 5.0f;

        // Duties module (2026-11-03): intercepts are offered as needs, Apply writes them.
        AssignStage GetStage() const override { return AssignStage::Duties; }
        char const* GetId() const override { return "focus"; }
        void Offer(WorldModel const& world, TeamPlan const& plan, std::vector<DutyNeed>& out) const override;
        void Apply(WorldModel const& world, std::vector<DutyResult> const& results, TeamPlan& plan) const override;
        void Clear(TeamPlan& plan) const override;

        // Kill priority of `enemy` (TargetAssigner: focus and every bot's kill order).
        static FocusScore Evaluate(WorldModel const& world, EnemyFacts const& enemy);
        // Can be the party focus: acting, not controlled, not a fresh enemy the tank has not settled.
        // Enemies only fighting an ally (onAllyOnly) are excluded unless `allowAllyOnly`.
        static bool IsEligible(WorldModel const& world, EnemyFacts const& enemy, bool allowAllyOnly = false);
    };
}

#endif
