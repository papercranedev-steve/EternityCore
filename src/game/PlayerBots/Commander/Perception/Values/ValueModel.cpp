#include "Commander/Perception/Values/ValueModel.h"
#include "Commander/Perception/WorldModel.h"
#include "Commander/Perception/Forecast/ThreatView.h"
#include "Commander/Perception/Tanks.h"
#include "SharedDefines.h"
#include "Timer.h"
#include <algorithm>
#include <cmath>

namespace Commander
{
namespace Value
{
    float ArmorReduction(uint32 armor, uint32 attackerLevel)
    {
        // SpellCaster::CalcArmorReducedDamage
        float tmp = 0.1f * float(armor) / (8.5f * float(attackerLevel) + 40.0f);
        tmp = tmp / (1.0f + tmp);
        return std::min(0.75f, std::max(0.0f, tmp));
    }

    float MeleeAvoidance(PartyMemberFacts const& victim, uint32 attackerLevel)
    {
        float const perSkill = victim.isPet ? 0.1f : 0.04f;
        float const skill = 5.0f * float(attackerLevel);
        float const miss = std::max(0.0f, 5.0f - (skill - float(victim.defenseSkill)) * perSkill);
        float const levelDiff = (skill - 5.0f * float(victim.level)) * perSkill;
        float const dodge = std::max(0.0f, victim.dodgePct - levelDiff);
        float const parry = victim.parryPct > 0.0f ? std::max(0.0f, victim.parryPct - levelDiff) : 0.0f;
        return std::min(0.95f, (miss + dodge + parry) / 100.0f);
    }

    float MeleeTaken(PartyMemberFacts const& victim, uint32 attackerLevel, bool physical)
    {
        float const armor = physical ? 1.0f - ArmorReduction(victim.armor, attackerLevel) : 1.0f;
        return armor * (1.0f - MeleeAvoidance(victim, attackerLevel));
    }

    float Points(WorldModel const& world, float health)
    {
        if (world.rates.partyMaxHealth <= 0.0f)
            return 0.0f;
        return health / world.rates.partyMaxHealth * 100.0f;
    }

    float CooldownPoints(float cooldownSec, float horizonSec)
    {
        if (cooldownSec <= GCD_SEC)
            return 0.0f;
        return COOLDOWN_POINTS_PER_MIN * std::min(cooldownSec, std::max(0.0f, horizonSec)) / 60.0f;
    }

    float ResetCost(WorldModel const& world, PartyMemberFacts const& caster,
                    CooldownResetFacts const& reset, float horizonSec)
    {
        if (!reset.spellId)
            return 0.0f;
        return CooldownPoints(reset.cooldownMs / 1000.0f, horizonSec) +
               (reset.gcd ? LostAction(world, caster, GCD_SEC) : 0.0f);
    }

    float DeathPenalty(PartyMemberFacts const& member)
    {
        if (member.HealsTeam())
            return DEATH_PENALTY_HEALER;
        if (Tanks::IsTank(member))      // bots and players (Tanks.h)
            return DEATH_PENALTY_TANK;
        return DEATH_PENALTY_OTHER;
    }

    namespace
    {
        // Before anything is measured: the last fight, scaled to the damage dealers there are
        // now, else the damage dealers' own estimates.
        float EstimatedDps(WorldModel const& world)
        {
            if (world.rates.lastFightDps > 0.0f && world.rates.lastFightDealers)
                return std::max(1.0f, world.rates.lastFightDps * float(world.rates.damageDealers) /
                                      float(world.rates.lastFightDealers));

            float estimate = 0.0f;
            for (PartyMemberFacts const& m : world.party)
                if (m.alive && m.DealsDamage())
                    estimate += m.soloDps > 0.0f ? m.soloDps : FALLBACK_DPS_PER_LEVEL * m.level;
            return std::max(1.0f, estimate);
        }

        char const* EstimatedDpsSource(WorldModel const& world)
        {
            if (world.rates.lastFightDps > 0.0f && world.rates.lastFightDealers)
                return "last fight";
            return "gear estimate";
        }

        bool FightMeasured(WorldModel const& world)
        {
            return world.rates.fightDpsPerDealer > 0.0f && world.rates.damageDealers;
        }
    }

    float DealtDps(WorldModel const& world)
    {
        if (world.rates.dealtMeasured && world.rates.dealtDps > 0.0f)
            return world.rates.dealtDps;
        return EstimatedDps(world);
    }

    char const* DealtDpsSource(WorldModel const& world)
    {
        if (world.rates.dealtMeasured && world.rates.dealtDps > 0.0f)
            return "measured";
        return EstimatedDpsSource(world);
    }

    float CapacityDps(WorldModel const& world)
    {
        if (FightMeasured(world))
            return std::max(1.0f, world.rates.fightDpsPerDealer * float(world.rates.damageDealers));
        return EstimatedDps(world);
    }

    float ActingDps(WorldModel const& world)
    {
        float perDealer = FightMeasured(world) ? world.rates.fightDpsPerDealer : 0.0f;
        if (world.rates.lastFightDps > 0.0f && world.rates.lastFightDealers)
            perDealer = std::max(perDealer, world.rates.lastFightDps / float(world.rates.lastFightDealers));
        if (perDealer <= 0.0f || !world.rates.damageDealers)
            return CapacityDps(world);
        return std::max(1.0f, perDealer * float(world.rates.damageDealers));
    }

    char const* CapacityDpsSource(WorldModel const& world)
    {
        if (FightMeasured(world))
            return "this fight";
        return EstimatedDpsSource(world);
    }

    float FightRemainingSec(WorldModel const& world)
    {
        return world.rates.enemyHealth / DealtDps(world);
    }

    float FightExtension(WorldModel const& world, float seconds)
    {
        return Points(world, std::max(0.0f, seconds) * world.rates.incomingDps);
    }

    float ContributionPerSec(WorldModel const& world, PartyMemberFacts const& member)
    {
        return ContributionPerSecAt(world, member, world.rates.worthDps);
    }

    float ContributionPerSecAt(WorldModel const& world, PartyMemberFacts const& member, float incomingDps)
    {
        if (!member.alive)
            return 0.0f;

        // By function: a healer that also deals damage (nobody else does) adds both parts.
        bool const heals = member.HealsTeam();
        float points = heals ? Points(world, incomingDps) / float(std::max(1u, world.rates.healers)) : 0.0f;

        // One second of this member's damage shortens the fight by share seconds,
        // which saves that much incoming damage. Pets: damage.
        if (!heals || member.DealsDamage())
        {
            float const share = 1.0f / float(std::max(1u, world.rates.damageDealers));
            points += Points(world, incomingDps) * share;
        }
        return points;
    }

    float LostAction(WorldModel const& world, PartyMemberFacts const& member, float seconds)
    {
        return ContributionPerSec(world, member) * std::max(0.0f, seconds);
    }

    float HitDps(EnemyFacts const& enemy, PartyMemberFacts const& victim)
    {
        if (!enemy.acting)
            return 0.0f;    // feigning death, divine shield, evading: hits nobody
        float const melee = enemy.meleePhysical
            ? enemy.meleeDps * (1.0f - ArmorReduction(victim.armor, enemy.level))
            : enemy.meleeDps;   // fire / frost melee ignores armor (resistances not modelled)
        return melee + enemy.rangedDps;
    }

    float SurviveSec(EnemyFacts const& enemy, PartyMemberFacts const& victim)
    {
        float const dps = HitDps(enemy, victim);
        if (dps <= 0.0f)
            return ThreatView::NO_LIMIT;
        return float(victim.health) / dps;
    }

    float IncomingOn(WorldModel const& world, ObjectGuid member)
    {
        PartyMemberFacts const* m = world.FindMember(member);
        if (!m)
            return 0.0f;
        float total = 0.0f;
        for (EnemyFacts const& e : world.enemies)
        {
            if (e.crowdControlled || e.victim != member)
                continue;
            // Rooted (Frost Nova) out of melee reach: only its ranged spell hits (2026-11-14:
            // the area caster that rooted its chasers counted their whole melee).
            float const dx = e.pos.x - m->pos.x, dy = e.pos.y - m->pos.y;
            if (e.rooted && e.acting && std::sqrt(dx * dx + dy * dy) - e.boundingRadius > ROOTED_REACH_YD)
                total += e.rangedDps;
            else
                total += HitDps(e, *m);
        }
        return total;
    }

    float TakenDps(WorldModel const& world, ObjectGuid member)
    {
        PartyMemberFacts const* m = world.FindMember(member);
        if (!m)
            return 0.0f;
        return std::max(IncomingOn(world, member), m->takenDps);
    }

    float HitLoss(WorldModel const& world, EnemyFacts const& enemy, PartyMemberFacts const& victim, float seconds)
    {
        seconds = std::max(0.0f, seconds);
        float const survive = SurviveSec(enemy, victim);
        float loss = Points(world, HitDps(enemy, victim) * std::min(seconds, survive));
        if (seconds >= survive)
            loss += DeathPenalty(victim);
        return loss;
    }

    float SurviveAllSec(WorldModel const& world, EnemyFacts const& enemy, PartyMemberFacts const& victim)
    {
        float dps = IncomingOn(world, victim.guid);
        if (enemy.victim != victim.guid || enemy.crowdControlled)
            dps += HitDps(enemy, victim);
        if (dps <= 0.0f)
            return ThreatView::NO_LIMIT;
        return float(victim.health) / dps;
    }

    float MoveLoss(WorldModel const& world, EnemyFacts const& enemy, PartyMemberFacts const& victim,
                   PartyMemberFacts const& holder, float seconds)
    {
        seconds = std::max(0.0f, seconds);
        float const onVictim = HitDps(enemy, victim);
        float const survive = SurviveAllSec(world, enemy, victim);
        float loss = Points(world, std::max(0.0f, onVictim - HitDps(enemy, holder)) * std::min(seconds, survive));
        if (seconds >= survive && onVictim > 0.0f)
        {
            float const total = float(victim.health) / survive;     // everything on the victim, per second
            loss += DeathPenalty(victim) * std::min(1.0f, onVictim / total);
        }
        return loss;
    }

    namespace
    {
        bool Chases(EnemyFacts const& e, ObjectGuid victim)
        {
            return e.acting && !e.crowdControlled && e.meleeEnabled && e.victim == victim;
        }
    }

    float HarassShare(WorldModel const& world, EnemyFacts const& enemy, PartyMemberFacts const& victim)
    {
        if (!victim.isBot || victim.isPet || !enemy.acting || enemy.crowdControlled || !enemy.meleeEnabled)
            return 0.0f;
        float share;
        switch (victim.role)
        {
            case ROLE_HEALER:    share = HARASS_SHARE_HEALER; break;
            case ROLE_RANGE_DPS: share = HARASS_SHARE_RANGED; break;
            default:             return 0.0f;
        }
        // Enemies chasing the victim, counting `enemy` also when it is not on it yet.
        uint32 chasers = enemy.victim == victim.guid ? 0 : 1;
        for (EnemyFacts const& e : world.enemies)
            if (Chases(e, victim.guid))
                ++chasers;
        return share / float(std::max(1u, chasers));
    }

    float HarassLoss(WorldModel const& world, EnemyFacts const& enemy, PartyMemberFacts const& victim, float seconds)
    {
        float const share = HarassShare(world, enemy, victim);
        if (share <= 0.0f)
            return 0.0f;
        seconds = std::min(std::max(0.0f, seconds), SurviveAllSec(world, enemy, victim));
        return LostAction(world, victim, seconds) * share;
    }

    float EnemyTimeToDie(WorldModel const& world, EnemyFacts const& enemy)
    {
        if (!enemy.acting)
            return ThreatView::NO_LIMIT;    // cannot be damaged right now
        // Damage is spread over the enemies that can be hit (not controlled ones we leave alone).
        size_t targets = 0;
        for (EnemyFacts const& e : world.enemies)
            if (e.acting && !e.crowdControlled && !e.onAllyOnly)
                ++targets;
        float const share = DealtDps(world) / float(std::max<size_t>(1, targets));
        return float(enemy.health) / std::max(1.0f, share);
    }

    float EnemyDyingSec(WorldModel const& world, EnemyFacts const& enemy)
    {
        bool const hurt = enemy.lastHurtMs &&
            WorldTimer::getMSTimeDiff(enemy.lastHurtMs, world.refreshedAtMs) <= uint32(HURT_RECENT_SEC * 1000.0f);
        return hurt ? EnemyTimeToDie(world, enemy) : ThreatView::NO_LIMIT;
    }
}
}
