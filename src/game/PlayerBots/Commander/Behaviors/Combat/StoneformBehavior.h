/*
 * PartyBot Commander layer 3 (cerebellum): a dwarf casts Stoneform as soon as a poison,
 * disease or bleed is on it (PartyMemberFacts::poisoned / diseased / bleeding), without
 * waiting for a party dispel (user 2026-11-15, cooldown doc .kiro/docs/2026-11-14-cooldown-spells.md 1).
 * Its other use, about to die (+10% armor), is the team rescue's (RaceKit::Survival).
 * A rule, not weighed against its 3 min cooldown: Routine tier, value = one global cooldown +
 * RULE_POINTS, cost = that global cooldown (heals, rescues and assigned tasks come first).
 */

#ifndef MANGOS_COMMANDER_STONEFORM_BEHAVIOR_H
#define MANGOS_COMMANDER_STONEFORM_BEHAVIOR_H

#include "Commander/Behaviors/IBehavior.h"

namespace Commander
{
    class StoneformBehavior : public IBehavior
    {
    public:
        static constexpr float RULE_POINTS = 1.0f;

        char const* GetId() const override { return "stoneform"; }
        bool AppliesTo(PartyBotAI const* bot) const override;
        void Propose(PartyBotAI const* bot, WorldModel const& world,
                     TeamPlan const& plan, std::vector<Proposal>& out) const override;
    };
}

#endif
