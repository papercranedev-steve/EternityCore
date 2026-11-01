/*
 * PartyBot Commander layer 3: healer mana budget (combat scenarios 9 and 13).
 * Formula and its limits: Perception/ManaBudget.h.
 *
 * Healer bots only, in combat, once the party's damage rate has been measured.
 * When the mana needed for the rest of the fight is more than the mana left (tension > 1):
 *  - Veto (VetoType::Heal) the heal the rotation would cast on a target at or above the heal
 *    ceiling for this tension (ManaBudget::HealCeilingPct; tanks keep a higher ceiling),
 *    unless the target would die within DELAY_TICKS bot ticks, or another member is below
 *    their ceiling (the veto would block FindAndHealInjuredAlly for them too this tick).
 *    Benefit: saved mana as the healing it buys later. Cost: damage taken while waiting.
 * Targets below EMERGENCY_PCT are never throttled. Which rank the rotation casts is chosen by
 * mana efficiency in CombatBotBaseAI::ChooseHeal (HealSpells.h); until 2026-11-01 this module
 * also swapped big heals for smaller, more efficient ranks.
 *
 * ThreatBehavior proposes heal vetoes and smaller heals for aggro on the same target; both
 * are scored in points, the arbiter keeps whatever is worth it.
 *
 * Low mana note: when ManaBudget::WantsLowManaWarning holds, the leader gets a system message
 * (same "[PartyBot]" form as plan notes), at most once per LOW_MANA_NOTE_REPEAT_MS per healer.
 * Sent directly, not through TeamPlan::notes: behaviors only get the plan read-only.
 *
 * Not done: future waves in the fight time, in-combat mana regeneration, ally NPC heal targets.
 */

#ifndef MANGOS_COMMANDER_MANA_BUDGET_BEHAVIOR_H
#define MANGOS_COMMANDER_MANA_BUDGET_BEHAVIOR_H

#include "Commander/Behaviors/IBehavior.h"

namespace Commander
{
    class ManaBudgetBehavior : public IBehavior
    {
    public:
        // Below this health percent the target is healed as the rotation wants.
        static constexpr float EMERGENCY_PCT = 30.0f;
        // A delayed heal waits about this many bot ticks (same as ThreatBehavior's heal veto).
        static constexpr float DELAY_TICKS = 2.0f;
        // The low mana note (ManaBudget::WantsLowManaWarning) is sent at most this often per healer.
        static constexpr uint32 LOW_MANA_NOTE_REPEAT_MS = 60000;

        struct Budget
        {
            float fightSec = 0.0f;       // T
            float mana = 0.0f;           // M
            float manaPct = 0.0f;        // M / mana pool, in percent (also while shapeshifted)
            float headroom = 0.0f;       // health the members being hit can lose without a heal
            float neededHealing = 0.0f;  // H (this healer's share)
            float healPerMana = 0.0f;    // E
            float neededMana = 0.0f;     // N
            float tension = 0.0f;        // N / M
        };

        // Map thread. False when the bot has no mana pool or the fight rate is not measured yet.
        static bool ComputeBudget(PartyBotAI const* bot, WorldModel const& world, Budget& out);

        char const* GetId() const override { return "mana"; }
        bool AppliesTo(PartyBotAI const* bot) const override;
        void Propose(PartyBotAI const* bot, WorldModel const& world,
                     TeamPlan const& plan, std::vector<Proposal>& out) const override;

    private:
        // Tells the leader when this healer is low on mana for the rest of the fight.
        // One behavior object serves every bot on every map thread: the last note time per
        // healer is kept in a locked table in the .cpp.
        void NoteLowMana(PartyBotAI const* bot, Budget const& budget) const;
    };
}

#endif
