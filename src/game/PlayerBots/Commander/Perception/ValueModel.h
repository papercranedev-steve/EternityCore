/*
 * PartyBot Commander: the common value unit (value-scoring doc section 3).
 *
 * 1 point = 1% of the party's total max health (pets excluded). Every benefit and
 * cost is converted to "party health saved or lost", so proposals from different
 * modules can be compared directly. Pure functions on WorldModel facts.
 */

#ifndef MANGOS_COMMANDER_VALUE_MODEL_H
#define MANGOS_COMMANDER_VALUE_MODEL_H

#include "Common.h"

namespace Commander
{
    struct WorldModel;
    struct PartyMemberFacts;
    struct EnemyFacts;

    namespace Value
    {
        // Death penalty on top of the lost health (user approved 2026-11-01).
        static constexpr float DEATH_PENALTY_HEALER = 30.0f;
        static constexpr float DEATH_PENALTY_TANK = 30.0f;
        static constexpr float DEATH_PENALTY_OTHER = 15.0f;

        // Global cooldown used for the time cost of an instant ability.
        static constexpr float GCD_SEC = 1.5f;
        // Damage per second per level of one member, used before the fight has been measured.
        static constexpr float FALLBACK_DPS_PER_LEVEL = 6.0f;

        // Armor mitigation of melee hits, vanilla formula (Unit::CalcArmorReducedDamage), max 75%.
        float ArmorReduction(uint32 armor, uint32 attackerLevel);

        // Health amount -> points.
        float Points(WorldModel const& world, float health);

        float DeathPenalty(PartyMemberFacts const& member);

        // Damage the party deals per second (measured, or the fallback estimate).
        float DealtDps(WorldModel const& world);
        // Seconds until all current enemies are dead at the current rate.
        float FightRemainingSec(WorldModel const& world);
        // Points lost when the fight lasts `seconds` longer (incoming damage during that time).
        float FightExtension(WorldModel const& world, float seconds);

        // What one member contributes per second, in points per second:
        // healers keep up with incoming damage (shared between healers), others shorten the fight.
        float ContributionPerSec(WorldModel const& world, PartyMemberFacts const& member);
        // Points lost when `member` cannot act for `seconds` (control, global cooldown, cast time).
        float LostAction(WorldModel const& world, PartyMemberFacts const& member, float seconds);

        // Melee damage per second `enemy` deals to `victim` after the victim's armor.
        float HitDps(EnemyFacts const& enemy, PartyMemberFacts const& victim);
        // Seconds `victim` survives being hit by `enemy` alone, no heals.
        float SurviveSec(EnemyFacts const& enemy, PartyMemberFacts const& victim);
    }
}

#endif
