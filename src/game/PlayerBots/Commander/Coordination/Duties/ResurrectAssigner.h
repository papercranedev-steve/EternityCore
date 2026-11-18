/*
 * PartyBot Commander layer 2 (brain): who brings a dead healer or tank back while the party
 * fights (user 2026-11-15, cooldown doc .kiro/docs/2026-11-14-cooldown-spells.md 10: Rebirth,
 * ClassKit::CombatResurrection). Duties module, the BoostAssigner pattern: while the party
 * fights, one need per corpse here (WorldModel::corpses) without a pending request; candidates =
 * bots whose combatRes is ready, within its range of the body / corpse (the behavior does not
 * walk there), net > 0 (Perception/Values/ResurrectValue.h: healers and tanks only); rank = net
 * (+ STICKY_POINTS for last refresh's choice). ResurrectBehavior casts it.
 * Out of combat every resurrection stays TeamBotAI's (UpdateOutOfCombatAI).
 */

#ifndef MANGOS_COMMANDER_RESURRECT_ASSIGNER_H
#define MANGOS_COMMANDER_RESURRECT_ASSIGNER_H

#include "Commander/Coordination/IAssigner.h"

namespace Commander
{
    class ResurrectAssigner : public IAssigner
    {
    public:
        static constexpr float STICKY_POINTS = 0.5f;

        // Same id as ResurrectBehavior.
        char const* GetId() const override { return "resurrect"; }
        AssignStage GetStage() const override { return AssignStage::Duties; }
        void Offer(WorldModel const& world, TeamPlan const& plan, std::vector<DutyNeed>& out) const override;
        void Apply(WorldModel const& world, std::vector<DutyResult> const& results, TeamPlan& plan) const override;
        void Clear(TeamPlan& plan) const override;
    };
}

#endif
