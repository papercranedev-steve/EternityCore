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
    struct CooldownResetFacts;

    namespace Value
    {
        // Death penalty on top of the lost health (user approved 2026-11-01).
        static constexpr float DEATH_PENALTY_HEALER = 30.0f;
        static constexpr float DEATH_PENALTY_TANK = 30.0f;
        static constexpr float DEATH_PENALTY_OTHER = 15.0f;

        // Global cooldown used for the time cost of an instant ability.
        static constexpr float GCD_SEC = 1.5f;
        // A rooted enemy hits in melee only this far from its edge (IncomingOn).
        static constexpr float ROOTED_REACH_YD = 5.0f;
        // Damage per second per level of one member whose own estimate is unknown
        // (PartyMemberFacts::soloDps 0), used before the fight has been measured.
        static constexpr float FALLBACK_DPS_PER_LEVEL = 6.0f;
        // PartyMemberFacts::soloDps (untested guesses, 2026-11-08). 6 x level gave 1440/s for
        // a level 60 party that dealt 435/s, and the pull estimate said 38 s for a 128 s fight
        // (strat-20261108-141921). Melee: white hits from the weapon fields, abilities on top.
        // Casters: the filler chain cast, less the time spent moving, wanding and on other spells.
        static constexpr float MELEE_ABILITY_FACTOR = 1.4f;
        static constexpr float CASTER_UPTIME = 0.6f;

        // What a cooldown used now costs (cooldown doc .kiro/docs/2026-11-14-cooldowns.md 5):
        // COOLDOWN_POINTS_PER_MIN for every minute it is missing, up to `horizonSec` (the fight
        // left; callers add the next pulls for spells that matter there too). 0 for a cooldown
        // of a GCD or less. The one place long cooldowns are judged "worth saving".
        static constexpr float COOLDOWN_POINTS_PER_MIN = 3.0f;
        float CooldownPoints(float cooldownSec, float horizonSec);
        // What casting the reset of an option first adds (CooldownResetFacts: Cold Snap,
        // Preparation): its cooldown points up to `horizonSec`, and one action when it starts
        // the global cooldown. 0 without a reset.
        float ResetCost(WorldModel const& world, PartyMemberFacts const& caster,
                        CooldownResetFacts const& reset, float horizonSec);

        // Armor mitigation of melee hits, vanilla formula (Unit::CalcArmorReducedDamage), max 75%.
        float ArmorReduction(uint32 armor, uint32 attackerLevel);
        // Share of a creature's melee swings at `victim` that miss, are dodged or parried
        // (SpellCaster::RollMeleeOutcomeAgainst, attacker weapon skill 5 x level): miss 5% minus
        // per-skill x (skill - defense), dodge and parry minus per-skill x (skill - 5 x the
        // victim's level); per-skill 0.04% on players, 0.1% on pets. Block (a reduction, not an
        // avoidance) and crushing blows are left to the caller's margin. 0..0.95.
        float MeleeAvoidance(PartyMemberFacts const& victim, uint32 attackerLevel);
        // Melee damage `victim` takes per point an attacker of that level swings: armor (only
        // for physical melee) and MeleeAvoidance (any school: a fire swing is dodged too).
        float MeleeTaken(PartyMemberFacts const& victim, uint32 attackerLevel, bool physical);

        // Health amount -> points.
        float Points(WorldModel const& world, float health);

        float DeathPenalty(PartyMemberFacts const& member);

        // Damage the party deals per second: measured in this fight, else the last fight's
        // (RateFacts::lastFightDps, scaled by the number of damage dealers), else the sum of
        // the damage dealers' own estimates (PartyMemberFacts::soloDps; FALLBACK_DPS_PER_LEVEL
        // x level where unknown).
        float DealtDps(WorldModel const& world);
        // Where DealtDps comes from now: "measured", "last fight" or "gear estimate".
        char const* DealtDpsSource(WorldModel const& world);
        // What the party deals per second over a fight, for forecasts of a fight (FightLoad):
        // this fight so far (RateFacts::fightDpsPerDealer x the damage dealers that can act
        // now), else as DealtDps without its few-second measurement. DealtDps stays the
        // current pace ("how soon does this enemy die").
        float CapacityDps(WorldModel const& world);
        // Where CapacityDps comes from now: "this fight", "last fight" or "gear estimate".
        char const* CapacityDpsSource(WorldModel const& world);
        // CapacityDps with every damage dealer acting (2026-11-15, user): per dealer the larger
        // of this fight's and the last fight's, x the damage dealers that can act. This fight's
        // pace counts a dealer stuck out of reach as 0 (stockade-20261115-012103: 116/s with the
        // mage in the corridor, the last fights 210/s and more); a forecast of what happens once
        // it walks in must not price it at that pace. The gear estimate is not taken (it ran
        // twice the real damage). Nothing measured: CapacityDps.
        float ActingDps(WorldModel const& world);
        // Seconds until all current enemies are dead at the current rate.
        float FightRemainingSec(WorldModel const& world);
        // Points lost when the fight lasts `seconds` longer (incoming damage during that time).
        float FightExtension(WorldModel const& world, float seconds);

        // What one member contributes per second, in points per second:
        // healers keep up with incoming damage (shared between healers), others shorten the fight.
        // At RateFacts::worthDps (the controlled enemies' damage when nothing hits us now).
        float ContributionPerSec(WorldModel const& world, PartyMemberFacts const& member);
        // The same in a fight with `incomingDps` damage per second on us: one that has not
        // started yet (the pull's FightLoad estimate, PullPlan::expectedIncomingDps).
        float ContributionPerSecAt(WorldModel const& world, PartyMemberFacts const& member, float incomingDps);
        // Points lost when `member` cannot act for `seconds` (control, global cooldown, cast time).
        float LostAction(WorldModel const& world, PartyMemberFacts const& member, float seconds);

        // Melee damage per second `enemy` deals to `victim` after the victim's armor.
        float HitDps(EnemyFacts const& enemy, PartyMemberFacts const& victim);
        // Seconds `victim` survives being hit by `enemy` alone, no heals.
        float SurviveSec(EnemyFacts const& enemy, PartyMemberFacts const& victim);

        // Melee damage per second all enemies currently deal to `member`, after armor (a
        // rooted one beyond ROOTED_REACH_YD of it: only its ranged damage).
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
        // Seconds until `enemy` dies of the damage it is taking: EnemyTimeToDie while it is being
        // hurt (lost health within HURT_RECENT_SEC, EnemyFacts::lastHurtMs), else NO_LIMIT. For
        // "it dies before its cast ends" (CastValue): nobody hitting it, it does not
        // (strat-20261114-133225: a 10 s summon on a 270 hp Eye of Naxxramas nobody hit was
        // worth 5 points at the start, the Counterspell came 1.2 s before the end).
        static constexpr float HURT_RECENT_SEC = 3.0f;
        float EnemyDyingSec(WorldModel const& world, EnemyFacts const& enemy);

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
