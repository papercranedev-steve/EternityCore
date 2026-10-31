/*
 * PartyBot Commander layer 3: dispel concern (value-scoring doc 4.2, 5.2).
 *
 * Vetoes dispelling a debuff that its caster puts straight back: the caster is
 * alive, hitting the debuffed member, and its profile says the spell is reapplied
 * (on-hit proc aura, or a spell list entry repeating every few seconds).
 * The veto is only kept when the dispel costs more than the debuff would do
 * before it comes back. Control effects are never vetoed here.
 * Out of combat the vetoes expire and the original rotation dispels as before.
 */

#ifndef MANGOS_COMMANDER_DISPEL_BEHAVIOR_H
#define MANGOS_COMMANDER_DISPEL_BEHAVIOR_H

#include "Commander/Behaviors/IBehavior.h"

namespace Commander
{
    class DispelBehavior : public IBehavior
    {
    public:
        // Seconds until an on-hit proc comes back when its chance is unknown.
        static constexpr float DEFAULT_REAPPLY_SEC = 4.0f;

        char const* GetId() const override { return "dispel"; }
        bool AppliesTo(PartyBotAI const* bot) const override;
        void Propose(PartyBotAI const* bot, WorldModel const& world,
                     TeamPlan const& plan, std::vector<Proposal>& out) const override;
    };
}

#endif
