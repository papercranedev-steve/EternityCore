/*
 * PartyBot Commander layer 2 (brain), situation step: the threat opening ("群拉",
 * .kiro/docs/2026-11-08-threat-modes.md, user 2026-11-08). Writes TeamPlan::threat.
 *
 * Why (strat-20261108-200046): the tank walked to its corner after the shot without a swing;
 * five of the six enemies on it had 0 tank threat; a Counterspell (180) and a few self buffs
 * (12-14) turned them. The old opening hold (PullOpening) waited for "threat > 0" at most 4 s
 * and nothing made the tank build it.
 *
 * Starts when an enemy is next to the main tank bot (within REACH_YD of its edge), acting, not
 * controlled, fighting the party, not settled yet, and the tank lacks threat on it
 * (ThreatRunway shortfall: enough to keep it through the healer's next heal; under the Area
 * tactic also through the whole area spell the doctrine counts on, DoctrinePlan::areaThreat /
 * areaSec, user 2026-11-12, doc .kiro/docs/2026-11-11-aoe-threat.md 4.3. The doctrine runs
 * after this module: the last refresh's. Every enemy next to the tank counts as in the pile:
 * a new one has no stamp yet). Also on while
 * the pulled enemies still walk to the tank (pull Settle past the arrival wait, an expected
 * enemy on the tank with melee on, further than REACH_YD; at most PullOpening::HOLD_MAX_MS of
 * Settle): the first hit of a damage dealer would take them.
 * While on: every bot except the tank waits (no cast, no attack: Behaviors/Constraints.h,
 * PartyBotAI::PassesPartyCastChecks, TargetAssigner holds their targets). Healers heal again
 * once someone is about to die (SurvivalValue danger, or below HEAL_BELOW_PCT); a member in
 * danger may act itself. The tank's task: `gaps` (TankTargets switches to the largest one,
 * ThreatBehavior values its area threat by the gaps it fills; both in points: the waiting
 * damage dealers' output, ThreatGap::WaitCostPerSec, x the seconds of tank threat saved).
 * Ends: every gap filled (the enemies are settled); or no progress (the total gap did not
 * shrink for STALL_MS: no rage, no ability reaches, someone keeps adding threat); or the tank
 * cannot act (dead, controlled). No fixed timeout (user 2026-11-08). Enemies it gave up on are
 * settled too: from then on the top-up and the rescue handle them (TankTargets, RescueAssigner).
 * While the opening runs, a settled enemy short again (the tank left it for the others) is back
 * in `gaps`; it does not count as a new enemy for the stall check.
 * Each start and end is a note.
 * Pure (facts and plan).
 */

#ifndef MANGOS_COMMANDER_THREAT_OPENING_H
#define MANGOS_COMMANDER_THREAT_OPENING_H

#include "Commander/Coordination/IAssigner.h"

namespace Commander
{
    class ThreatOpeningAssigner : public IAssigner
    {
    public:
        static constexpr float REACH_YD = 8.0f;             // next to the tank (from the enemy's edge)
        static constexpr uint32 STALL_MS = 3000;
        static constexpr float PROGRESS_MIN = 1.0f;         // threat: the total gap shrank
        static constexpr float HEAL_BELOW_PCT = 50.0f;      // = PullBehavior::HOLD_HEAL_BELOW_PCT

        char const* GetId() const override { return "opening"; }
        AssignStage GetStage() const override { return AssignStage::Situation; }
        void Assign(WorldModel const& world, TeamPlan& plan) const override;
        void Clear(TeamPlan& plan) const override;
    };
}

#endif
