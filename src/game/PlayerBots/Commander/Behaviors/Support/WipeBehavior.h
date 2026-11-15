/*
 * PartyBot Commander layer 3 (cerebellum): carries out the wipe protection (TeamPlan::wipe,
 * Coordination/Situation/Crisis/WipeAssigner.h) while the lost fight goes on.
 *
 *   stone   the warlock bot uses its Soulstone (UseItem) on TeamPlan::wipe.stoneOn, Urgent,
 *           when in range and sight (else it fights on and tries again next tick).
 *   hider   not hidden yet: casts its Feign Death / Vanish, Urgent. Hidden (feigning death, or
 *           stealthed after Vanish): Override Hold, nothing else (no target, no heal, no
 *           movement) until the fight is over. A feigning bot does not reach the arbiter anyway
 *           (PartyBotAI::UpdateAI: it cannot react); it gets up by itself once no enemy is near.
 */

#ifndef MANGOS_COMMANDER_WIPE_BEHAVIOR_H
#define MANGOS_COMMANDER_WIPE_BEHAVIOR_H

#include "Commander/Behaviors/IBehavior.h"

namespace Commander
{
    class WipeBehavior : public IBehavior
    {
    public:
        static constexpr float URGENT_SCORE = 50.0f;

        char const* GetId() const override { return "wipe"; }
        bool AppliesTo(PartyBotAI const* bot) const override;
        void Propose(PartyBotAI const* bot, WorldModel const& world,
                     TeamPlan const& plan, std::vector<Proposal>& out) const override;
    };
}

#endif
