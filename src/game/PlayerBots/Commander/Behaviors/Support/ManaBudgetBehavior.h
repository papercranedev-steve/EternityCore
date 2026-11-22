/*
 * PartyBot Commander layer 3: healer mana budget (combat scenarios 9 and 13).
 * Formula and its limits: Perception/Forecast/ManaBudget.h.
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
 * Low mana note (debug data): when ManaBudget::WantsLowManaWarning holds, the healer says it in
 * party chat while debug speech is on (BotSpeech::Report), at most once per
 * LOW_MANA_NOTE_REPEAT_MS per healer. What players hear is the party voice's "fight.oom".
 * Sent directly, not through TeamPlan::notes: behaviors only get the plan read-only.
 *
 * Not done: future waves in the fight time, in-combat mana regeneration, ally NPC heal targets.
 *
 * Self restore (any role whose class kit has one: ClassKit::SelfManaRestore, mage Evocation;
 * user, 2026-11-01: an emergency resource, only in combat, never right after the fight):
 * in combat, not hit by anything, the fight rate measured, the spell castable. The filler
 * (ClassKit::Filler) spends S = mana / busy time - regeneration while casting per second.
 * Over the rest of the fight T (Value::FightRemainingSec) the bot casts
 *   without:  min(T, M / S)
 *   with:     min(T - channel, (M + R) / S),  R = regeneration outside the five second rule x
 *             (1 + the spell's power regen percent / 100) x channel, capped at the empty pool.
 * Proposed (Cast, tier Assigned) with benefit LostAction(with), cost LostAction(without):
 * only when channelling now leaves more casting time than running dry at the end.
 * Timing (2026-11-02, strat-20261102-220932: Evocation at 79% mana, 1100 of ~4000 restored):
 * while the missing mana is less than the uncapped R, every filler cast first makes the same
 * channel restore more, so it waits until R fits, or until the mana pays fewer than
 * DRY_FILLER_CASTS fillers (running dry before R fits: big regeneration pool).
 *
 * Mana items (any bot with a mana pool; ManaItemAbility facts from Classes/ItemKit.h: mana
 * potions, mana gems, Demonic / Dark Rune; cooldown doc 2026-11-14, spell list section 5):
 * in combat, the fight rate measured, only once the missing mana is at least the item's
 * average R (nothing of it wasted). Instant, off the global cooldown (BotActionType::UseItem).
 * Consumables come after own spells and only when the fight is not won without them (user
 * 2026-11-15): none while a boost duty (Innervate, Mana Tide) targets the bot or its own
 * restore (Evocation) is ready and nothing hits it; a damage dealer only in a boss fight
 * (running dry elsewhere only slows the kill); a healer only while the mana does not last the
 * fight (the benefit below is 0 at tension <= 1).
 *   benefit  healer:  Points(ManaBudget::SavedManaHealth(R, E, tension)) (0 while the mana lasts)
 *            others:  LostAction(min(T, (M + R) / S)) - LostAction(min(T, M / S)), S as above
 *   cost     Value::CooldownPoints(item cooldown, T + SurvivalValue::NEXT_FIGHTS_SEC)
 *            + Points(the rune's average damage to itself)
 * A rune is skipped while the bot is in danger (SurvivalValue) or when it would leave it below
 * RUNE_MIN_HEALTH_PCT. The best net item is proposed at tier Assigned.
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
        // Self restore before its whole amount fits only when the mana pays fewer fillers than this.
        static constexpr float DRY_FILLER_CASTS = 2.0f;
        // A rune (it hurts the user) only while its damage leaves at least this health percent. Guess.
        static constexpr float RUNE_MIN_HEALTH_PCT = 50.0f;

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
        static bool ComputeBudget(TeamBotAI const* bot, WorldModel const& world, Budget& out);

        char const* GetId() const override { return "mana"; }
        bool AppliesTo(TeamBotAI const* bot) const override;
        void Propose(TeamBotAI const* bot, WorldModel const& world,
                     TeamPlan const& plan, std::vector<Proposal>& out) const override;

    private:
        // Self restore (header): mage Evocation when it buys casting time.
        void ProposeSelfRestore(TeamBotAI const* bot, WorldModel const& world, std::vector<Proposal>& out) const;
        // Mana items (header): the best mana potion, gem or rune when it pays.
        void ProposeManaItem(TeamBotAI const* bot, WorldModel const& world, TeamPlan const& plan,
                             std::vector<Proposal>& out) const;
        void ProposeHealVeto(TeamBotAI const* bot, WorldModel const& world, std::vector<Proposal>& out) const;
        // Tells the leader when this healer is low on mana for the rest of the fight.
        // One behavior object serves every bot on every map thread: the last note time per
        // healer is kept in a locked table in the .cpp.
        void NoteLowMana(TeamBotAI const* bot, Budget const& budget) const;
    };
}

#endif
