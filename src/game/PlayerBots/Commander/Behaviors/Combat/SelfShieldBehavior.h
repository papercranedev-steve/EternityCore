/*
 * PartyBot Commander layer 3 (cerebellum): a bot's own shield (ClassKit::SelfShield: Ice
 * Barrier) up whenever it pays (user 2026-11-28: "if he has it, cast it": under Unholy Aura it
 * takes hits the healer would heal and keeps the Frostbolts from being pushed back). Party heal
 * doc .kiro/docs/2026-11-28-party-heal-renew.md "Pushback".
 *
 * In the party's fight, the shield ready and not up (PartyMemberFacts::selfShield), castable:
 * ShieldValue::Of (the hits it takes within ShieldValue::HORIZON_SEC + the casting time their
 * pushback would cost) against one GCD of the bot (Value::LostAction) + its mana (ManaWorth).
 * Assigned tier, value = that benefit and cost. The area phase's shield before an area spell
 * and EscapeBehavior's danger shield stay as they are; whichever comes first puts it up.
 */

#ifndef MANGOS_COMMANDER_SELF_SHIELD_BEHAVIOR_H
#define MANGOS_COMMANDER_SELF_SHIELD_BEHAVIOR_H

#include "Commander/Behaviors/IBehavior.h"

namespace Commander
{
    class SelfShieldBehavior : public IBehavior
    {
    public:
        char const* GetId() const override { return "selfshield"; }
        bool AppliesTo(TeamBotAI const* bot) const override;
        void Propose(TeamBotAI const* bot, WorldModel const& world,
                     TeamPlan const& plan, std::vector<Proposal>& out) const override;
    };
}

#endif
