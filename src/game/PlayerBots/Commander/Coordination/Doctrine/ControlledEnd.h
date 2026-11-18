/*
 * PartyBot Commander layer 2 (brain), situation: only controlled enemies are left (crowd
 * control doc .kiro/docs/2026-11-07/2026-11-07-crowd-control.md 4.4, todo 10d). Before, the
 * party stood idle until the control ran out or the player attacked: bots do not pick an
 * enemy under a control that breaks on damage (CombatBotBaseAI::IsValidHostileTarget).
 *
 * Active while the party fights and every acting enemy of ours is under a control that breaks
 * on damage (EnemyFacts::control: polymorph, sap, shackle, sleep; a stun or fear the party
 * hits through is not this situation). The enemy whose control ends first is woken first, by the
 * main tank (Perception/Tanks.h; none: nothing is planned).
 * Waiting (since 2026-11-13, user: standing in combat to regain mana is only for a fight the
 * party could not get through otherwise, the healer running dry under the tank; before, mana
 * regained was valued in points against WAIT_POINTS_PER_SEC, and strat-20261113-001309 waited
 * 31 s for 2.0 vs 1.6 points next to one shackled Ghoul Ravener with the healer at 66% mana and
 * the forecast ending at 1771 mana):
 *   the fight still to come = FightLoad of the controlled enemies, all at once
 *   bearable now (FightLoad::Bearable: the tank lives and stays above TANK_FLOOR_PCT): wake it now
 *   else the shortest wait W, in WAIT_STEP_SEC steps up to its control left - BREAK_LEAD_SEC
 *   - the tank's walk to it (RUN_YD_PER_SEC), after which it is bearable (FightLoad::AfterWait:
 *   healers regain mana outside the five second rule); none is: the longest wait if that fight
 *   scores better (FightLoad::Score) than now, else now
 *   waiting is off when an idle patrol comes to the enemy (Perception/Spatial/PatrolZone.h)
 *   within W + that fight
 * Decided once per enemy (kept while it sleeps); notes tell the leader. The crowd control
 * assigner gives nobody a control on it meanwhile (no recast while waiting to wake it).
 * Area pile: nobody wakes a controlled enemy in it on purpose (user 2026-11-15). The doctrine
 * stamps it AreaMayWake (TacticalDoctrine.h): the area spells go on, hit it wakes and joins
 * the pile, missed it sleeps until the case above. From 2026-11-09 to 2026-11-15 the main tank
 * was sent to wake it at once (strat-20261115-134307: it left the pile while no area spell was
 * coming, and the woken ghoul went to the mage).
 * Execution: TargetAssigner gives the tank the enemy (forced, ControlEndPlan::why);
 * TeamBotAI::IsAssignedToBreakControl lets the tank (only the tank, only that enemy) pick it.
 */

#ifndef MANGOS_COMMANDER_CONTROLLED_END_H
#define MANGOS_COMMANDER_CONTROLLED_END_H

#include "Commander/Coordination/IAssigner.h"

namespace Commander
{
    class ControlledEndAssigner : public IAssigner
    {
    public:
        static constexpr float BREAK_LEAD_SEC = 2.0f;
        static constexpr float RUN_YD_PER_SEC = 7.0f;
        static constexpr float WAIT_STEP_SEC = 5.0f;

        // Same id as the crowd control modules: `.partybot ai module cc off` switches all.
        char const* GetId() const override { return "cc"; }
        AssignStage GetStage() const override { return AssignStage::Situation; }
        void Assign(WorldModel const& world, TeamPlan& plan) const override;
        void Clear(TeamPlan& plan) const override;
    };
}

#endif
