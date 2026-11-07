/*
 * PartyBot Commander layer 3: casts the control spell the brain gave this bot ("cc" module,
 * crowd control doc .kiro/docs/2026-11-07/2026-11-07-crowd-control.md, todo 10b).
 *
 * Takeover::CrowdControl in combat: the rotations' own controls stand down (marked targets,
 * the mage's sheep on a second attacker, Blind, Hibernate, Shackle, Banish, Fear, Howl of
 * Terror, Intimidating Shout; PartyBotAI::IsCrowdControlControlled). Bots without control
 * spells (hunters: Scare Beast) keep theirs. Psychic Scream to save itself is EscapeBehavior's.
 *
 * Propose, for this bot's ControlDuty (TeamPlan::FindControlFor), checked live: the target is
 * alive and not under the same spell any more, in sight, CanTryToCastSpell (range, power,
 * cooldown, form, reagents). Tier Assigned, value = the duty's net points + the global
 * cooldown it costs, cost = that global cooldown (the arbiter compares by net). A control that
 * breaks on damage drops the bot's own attack on its target (BotAction::dropTarget).
 *
 * Known limit: CanTryToCastSpell refuses a spell whose aura the target still has, so a sheep
 * is cast again only once the old one ended (not RECAST_LEAD_SEC before).
 */

#ifndef MANGOS_COMMANDER_CROWD_CONTROL_BEHAVIOR_H
#define MANGOS_COMMANDER_CROWD_CONTROL_BEHAVIOR_H

#include "Commander/Behaviors/IBehavior.h"

namespace Commander
{
    class CrowdControlBehavior : public IBehavior
    {
    public:
        char const* GetId() const override { return "cc"; }
        bool AppliesTo(PartyBotAI const* bot) const override;
        bool TakesOver(Takeover what, PartyBotAI const* bot, WorldModel const& world) const override;
        void Propose(PartyBotAI const* bot, WorldModel const& world,
                     TeamPlan const& plan, std::vector<Proposal>& out) const override;
    };
}

#endif
