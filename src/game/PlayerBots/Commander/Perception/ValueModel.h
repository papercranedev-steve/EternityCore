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

        // Melee damage per second all enemies currently deal to `member`, after armor.
        float IncomingOn(WorldModel const& world, ObjectGuid member);
        // Damage per second `member` is expected to take: the larger of IncomingOn (predicted
        // from the enemies on it) and the measured rate (PartyMemberFacts::takenDps), which
        // also sees boss abilities and area spells, but lags behind a target switch.
        float TakenDps(WorldModel const& world, ObjectGuid member);

        // Points lost while `enemy` hits `victim` for `seconds`: the damage, plus the death
        // penalty if the victim does not survive that long.
        float HitLoss(WorldModel const& world, EnemyFacts const& enemy, PartyMemberFacts const& victim, float seconds);

        // Seconds `victim` survives everything hitting it now (IncomingOn), plus `enemy` if that
        // one is not on it yet; no heals.
        float SurviveAllSec(WorldModel const& world, EnemyFacts const& enemy, PartyMemberFacts const& victim);

        // Points lost while `enemy` hits `victim` for `seconds` instead of `holder` (the tank, or
        // whoever holds it now): the hits are taken either way, so only what `victim` takes on
        // top counts (armor: a caster's spells cost the same on both, zero), plus the death
        // penalty when `victim` dies within `seconds` of everything hitting it
        // (SurviveAllSec), weighted by this enemy's share of that damage.
        // (combat scenario: Stratholme Skeletal Guardians casting at the player, 2026-11-01)
        float MoveLoss(WorldModel const& world, EnemyFacts const& enemy, PartyMemberFacts const& victim,
                       PartyMemberFacts const& holder, float seconds);

        // Seconds until `enemy` dies at its share of the party's damage.
        float EnemyTimeToDie(WorldModel const& world, EnemyFacts const& enemy);

        // Share of a bot's output lost while melee enemies chase it (user, 2026-11-01): a healer's
        // casts are pushed back and it has to step around, a ranged damage dealer kites or is
        // pushed back. Melee damage dealers and tanks fight on (0), players are not modelled (0).
        // Only enemies with melee on (EnemyFacts::meleeEnabled): a caster standing back costs its
        // spell damage on anyone (MoveLoss) and nothing more. Several melee enemies on one bot
        // share it (taking one of three away saves a third).
        static constexpr float HARASS_SHARE_HEALER = 0.5f;
        static constexpr float HARASS_SHARE_RANGED = 0.3f;
        float HarassShare(WorldModel const& world, EnemyFacts const& enemy, PartyMemberFacts const& victim);
        // Points of `victim`'s output lost while `enemy` chases it for `seconds` (until it dies at most).
        float HarassLoss(WorldModel const& world, EnemyFacts const& enemy, PartyMemberFacts const& victim, float seconds);
    }
}

#endif
