/*
 * PartyBot Commander layer 3: dispel concern (value-scoring doc 4.2, 5.2).
 *
 * Vetoes dispelling a debuff that its caster puts straight back: the caster is
 * alive, hitting the debuffed member, and its profile says the spell is reapplied
 * (on-hit proc aura, or a spell list entry repeating every few seconds).
 * The veto is only kept when the dispel costs more than the debuff would do
 * before it comes back. Control effects are not vetoed for that reason; the one exception
 * is a control that breaks on damage (sleep) on a member who is being hit: the next hit
 * ends it anyway (combat scenario 3).
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
        // A sleep on someone being hit lasts about this long before the next hit breaks it.
        static constexpr float WAKE_ON_DAMAGE_SEC = 2.0f;

        char const* GetId() const override { return "dispel"; }
        bool AppliesTo(PartyBotAI const* bot) const override;
        void Propose(PartyBotAI const* bot, WorldModel const& world,
                     TeamPlan const& plan, std::vector<Proposal>& out) const override;
    };
}

#endif
