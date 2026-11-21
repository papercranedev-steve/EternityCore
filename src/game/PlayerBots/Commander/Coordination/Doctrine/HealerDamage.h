/*
 * PartyBot Commander layer 2 (doctrine): how much each healer bot may deal damage
 * ("healdps" assigner). Design: .kiro/docs/2026-11-21-healer-damage-tiers.md.
 *
 * Output: TeamPlan::healerDamage, a HealerDamageTier for every healer-role bot (m_role
 * ROLE_HEALER), every refresh. The rotations read it (step 4 of the doc); this module only decides.
 *
 * The tier, first rule that applies:
 *   set by the user        `.partybot healdps 0-3` (PartyMemberFacts::healerDamageSet)
 *   nobody else deals      the coverage gave it Damage (TeamPlan::coverage, added): FULL
 *   damage
 *   raid                   NONE
 *   dungeon                NONE; SPARE while the pressure is low (only facts of now, no forecast):
 *                            no acting boss, at most LOW_PRESSURE_ENEMIES acting enemies or all of
 *                            them below NEARLY_DEAD_PCT health, no member below
 *                            LOW_PRESSURE_HEALTH_PCT
 *   open world             FULL; SPARE while the fight forecast is hard (FightLoad from the world,
 *                          FightPlace::IsHard: the tank drops low, the healers end low on mana, or
 *                          not bearable)
 * Going down is at once. Going up waits for a change in the fight: the number of acting enemies
 * changed since the last refresh (one died, one joined, the fight started). So a tier does not
 * flip between two refreshes.
 * The open world forecast is made at such a change and every FORECAST_MS in between (a forecast
 * that turns hard lowers the tier at once).
 * Battlegrounds count as the open world (MapKind::World).
 * Melee (HealerDamageDuty::melee, doc step 5): a paladin or shaman at FREE or above in the open
 * world walks to its target and swings (MeleeStanceBehavior instead of HealerStanceBehavior,
 * melee reach for its target); its swings stop near aggro (TeamBotAI::FightVictim). Never in
 * dungeons and raids (user 2026-11-21).
 * No mana reserve (HealerDamageDuty::noReserve, doc section 5): FULL because nobody else deals
 * damage and no free attack (no wand): kept to a reserve it would stand idle and the enemy
 * would never die.
 */

#ifndef MANGOS_COMMANDER_HEALER_DAMAGE_H
#define MANGOS_COMMANDER_HEALER_DAMAGE_H

#include "Commander/Coordination/IAssigner.h"

namespace Commander
{
    struct PartyMemberFacts;

    class HealerDamageAssigner : public IAssigner
    {
    public:
        // Dungeon "low pressure" (doc section 11; guesses, to be tuned).
        static constexpr uint32 LOW_PRESSURE_ENEMIES = 1;
        static constexpr float NEARLY_DEAD_PCT = 20.0f;
        static constexpr float LOW_PRESSURE_HEALTH_PCT = 80.0f;
        // The open world forecast between two changes of the fight.
        static constexpr uint32 FORECAST_MS = 3000;
        // Mana conditions the bot applies each tick (TeamBotAI::HealerDamageNow; doc sections 3, 5):
        // SPARE needs this mana percent, FULL keeps RESERVE_HEALS x its biggest heal's mana.
        // Below them the tick is FREE.
        static constexpr float TIER2_MANA_PCT = 80.0f;
        static constexpr float RESERVE_HEALS = 2.0f;

        // `m`, a healer in the plan, gets a damage target (TargetAssigner): a damage function
        // (the coverage), or a tier with something to do: FREE with a free attack
        // (PartyMemberFacts::canShootWand), SPARE and FULL always.
        static bool Attacks(PartyMemberFacts const& m, TeamPlan const& plan);

        char const* GetId() const override { return "healdps"; }
        void Assign(WorldModel const& world, TeamPlan& plan) const override;
        void Clear(TeamPlan& plan) const override;

        // "none" / "free" / "spare" / "full".
        static char const* Name(uint8 tier);
    };
}

#endif
