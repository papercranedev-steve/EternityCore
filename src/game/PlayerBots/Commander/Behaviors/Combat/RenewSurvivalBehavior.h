/*
 * PartyBot Commander layer 3 (cerebellum): a survival spell marked renewWhileHit (Evasion) that
 * ran out while melee enemies still hit the bot is put on again right away, its cooldown reset
 * first (Preparation; ClassKit::CooldownReset), user 2026-11-15 ("still tanking when Evasion
 * ends: Preparation, Evasion again"; cooldown doc .kiro/docs/2026-11-14-cooldown-spells.md 10).
 *
 * Propose, for each such SurvivalAbility of the bot with a ready reset (CooldownResetFacts):
 * its aura is gone, it ended at most RENEW_WINDOW_MS ago (its cooldown left: cooldown -
 * duration - window or more), and an acting melee enemy is on the bot (SurvivalValue::Danger
 * physicalDps > 0). The cast: the reset, then the spell (BotAction::prepResets).
 * A rule, not weighed against the reset's 10 min cooldown: Routine tier, value = the melee
 * damage it keeps off (Value::Points over its duration) + the reset's global cooldown, cost =
 * that global cooldown. About to die is the team rescue's (it may pick the same cast).
 */

#ifndef MANGOS_COMMANDER_RENEW_SURVIVAL_BEHAVIOR_H
#define MANGOS_COMMANDER_RENEW_SURVIVAL_BEHAVIOR_H

#include "Commander/Behaviors/IBehavior.h"

namespace Commander
{
    class RenewSurvivalBehavior : public IBehavior
    {
    public:
        // Untested guess: long enough for a busy tick or a global cooldown in the way.
        static constexpr uint32 RENEW_WINDOW_MS = 5000;

        char const* GetId() const override { return "renew"; }
        bool AppliesTo(PartyBotAI const* bot) const override;
        void Propose(PartyBotAI const* bot, WorldModel const& world,
                     TeamPlan const& plan, std::vector<Proposal>& out) const override;
    };
}

#endif
