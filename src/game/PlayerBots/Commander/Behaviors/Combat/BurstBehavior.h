/*
 * PartyBot Commander layer 3 (cerebellum): the bot's damage cooldowns ("burst" module, cooldown
 * doc .kiro/docs/2026-11-14-cooldowns.md section 6). Bots that are not healers.
 *
 * Every tick while the party fights: each damage cooldown in PartyMemberFacts::bursts
 * (ClassKit::Bursts, RaceKit::Bursts) that passes its gates and is worth more than it costs
 * (Perception/Values/BurstValue.h) is proposed at Routine tier, scored by its net worth:
 *   a buff        cast on itself (Bestial Wrath: on its pet)
 *   a pairing     the cast it boosts on the victim, with the cooldown put on right before it
 *                 (BotAction::prepSpell: Cold Blood + Eviscerate, Presence of Mind + Pyroblast,
 *                 Elemental Mastery + Chain Lightning, Amplify Curse + Curse of Agony); only
 *                 when that cast can go now (for Amplify Curse: the curse not up on the victim)
 * Takes over Takeover::Burst: the rotations' own uses of these spells stand down
 * (PartyBotAI::IsBurstControlled; duels keep them).
 */

#ifndef MANGOS_COMMANDER_BURST_BEHAVIOR_H
#define MANGOS_COMMANDER_BURST_BEHAVIOR_H

#include "Commander/Behaviors/IBehavior.h"

namespace Commander
{
    class BurstBehavior : public IBehavior
    {
    public:
        char const* GetId() const override { return "burst"; }
        bool AppliesTo(PartyBotAI const* bot) const override;
        bool TakesOver(Takeover what, PartyBotAI const* bot, WorldModel const& world, TeamPlan const& plan) const override;
        void Propose(PartyBotAI const* bot, WorldModel const& world,
                     TeamPlan const& plan, std::vector<Proposal>& out) const override;
    };
}

#endif
