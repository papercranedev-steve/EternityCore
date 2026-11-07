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
 *   wait W  = its control left - BREAK_LEAD_SEC - the tank's walk to it (RUN_YD_PER_SEC)
 *   gain    = mana the party's mana users regenerate in W (PartyMemberFacts::manaRegen, the
 *             rate outside the five second rule; in combat nobody eats or drinks), capped at
 *             their missing mana, valued by ManaWorth for the fight still to come (FightLoad
 *             of the controlled enemies, all at once)
 *   cost    = WAIT_POINTS_PER_SEC x W; waiting is off when an idle patrol's walk comes within
 *             its reach + PATROL_MARGIN_YD of the enemy within W + that fight
 * gain > cost: wake it W seconds from now, else now. Decided once per enemy (kept while it
 * sleeps); notes tell the leader. The crowd control assigner gives nobody a control on it
 * meanwhile (no recast while waiting to wake it).
 * Execution: CrowdControlBehavior has the tank attack it once breakAtMs passed;
 * PartyBotAI::IsAssignedToBreakControl lets the tank (only the tank, only that enemy) pick it.
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
        static constexpr float WAIT_POINTS_PER_SEC = 0.05f;    // untested guess
        static constexpr float PATROL_MARGIN_YD = 5.0f;

        // Same id as the crowd control modules: `.partybot ai module cc off` switches all.
        char const* GetId() const override { return "cc"; }
        AssignStage GetStage() const override { return AssignStage::Situation; }
        void Assign(WorldModel const& world, TeamPlan& plan) const override;
        void Clear(TeamPlan& plan) const override;
    };
}

#endif
