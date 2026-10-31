/*
 * PartyBot Commander layer 3: executes interrupt duties from InterruptAssigner (step 4),
 * scored as benefit - cost (value-scoring doc 5.1).
 * Design: .kiro/docs/2026-11-01-interrupt-assignment.md
 *
 *  Primary - interrupt as soon as the cast is seen. Melee primaries out of range switch
 *            to the caster so the original chase logic brings them in.
 *  Backup  - interrupt when the primary cannot (controlled, no ready ability that the enemy
 *            is not immune to, busy casting, gone) or the cast is past BACKUP_AFTER_PROGRESS.
 *  Abilities the enemy is immune to (CastValues::CanStop) are never proposed; each ability's
 *            benefit includes its school lockout (CastValues::LockoutBenefit).
 *  Own target - if the cast of the bot's own target is not worth an interrupt, veto the
 *            original rotation's reflex interrupt (VetoType::Interrupt).
 *  Urgent tier only when the cast would kill someone; otherwise team-task tier.
 */

#ifndef MANGOS_COMMANDER_INTERRUPT_BEHAVIOR_H
#define MANGOS_COMMANDER_INTERRUPT_BEHAVIOR_H

#include "Commander/Behaviors/IBehavior.h"

namespace Commander
{
    class InterruptBehavior : public IBehavior
    {
    public:
        static constexpr float BACKUP_AFTER_PROGRESS = 0.5f;
        static constexpr float MELEE_ABILITY_RANGE = 5.0f;

        char const* GetId() const override { return "interrupt"; }
        bool AppliesTo(PartyBotAI const* bot) const override;
        void Propose(PartyBotAI const* bot, WorldModel const& world,
                     TeamPlan const& plan, std::vector<Proposal>& out) const override;

    private:
        void ProposeOwnTargetVeto(PartyBotAI const* bot, WorldModel const& world,
                                  std::vector<Proposal> const& made, std::vector<Proposal>& out) const;
    };
}

#endif
