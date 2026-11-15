/*
 * PartyBot Commander layer 3 (cerebellum): a tank bot keeps its attack power debuff on the
 * enemies around it ("mitigation" module; PartyMemberFacts::mitigations: Demoralizing Shout /
 * Roar). Routine tier: a threat emergency, a rescue or an assigned task comes first. Value and
 * cost: Perception/Values/MitigationValue.h (the damage it takes off vs a global cooldown and
 * its rage); nothing while it would pull a creature out of combat. A warrior casts through
 * PartyBotAI::DoCastWithStance (the Arbiter); Demoralizing Shout needs no swap.
 * The same spells stay in ClassKit::AreaThreat: ThreatBehavior still proposes them for the
 * threat on loose enemies, valued apart (the Arbiter keeps the better one).
 */

#ifndef MANGOS_COMMANDER_MITIGATION_BEHAVIOR_H
#define MANGOS_COMMANDER_MITIGATION_BEHAVIOR_H

#include "Commander/Behaviors/IBehavior.h"

namespace Commander
{
    class MitigationBehavior : public IBehavior
    {
    public:
        char const* GetId() const override { return "mitigation"; }
        bool AppliesTo(PartyBotAI const* bot) const override;
        void Propose(PartyBotAI const* bot, WorldModel const& world,
                     TeamPlan const& plan, std::vector<Proposal>& out) const override;
    };
}

#endif
