#include "Commander/Perception/ValueModel.h"
#include "Commander/Perception/WorldModel.h"
#include "Commander/Perception/ThreatView.h"
#include "Commander/Perception/Tanks.h"
#include "SharedDefines.h"
#include <algorithm>

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

    float Points(WorldModel const& world, float health)
    {
        if (world.rates.partyMaxHealth <= 0.0f)
            return 0.0f;
        return health / world.rates.partyMaxHealth * 100.0f;
    }

    float DeathPenalty(PartyMemberFacts const& member)
    {
        if (member.isBot && member.role == ROLE_HEALER)
            return DEATH_PENALTY_HEALER;
        if (Tanks::IsTank(member))      // bots and players (Tanks.h)
            return DEATH_PENALTY_TANK;
        return DEATH_PENALTY_OTHER;
    }

    float DealtDps(WorldModel const& world)
    {
        if (world.rates.dealtMeasured && world.rates.dealtDps > 0.0f)
            return world.rates.dealtDps;
        // The last fight, scaled to the damage dealers there are now.
        if (world.rates.lastFightDps > 0.0f && world.rates.lastFightDealers)
            return std::max(1.0f, world.rates.lastFightDps * float(world.rates.damageDealers) /
                                  float(world.rates.lastFightDealers));

        float estimate = 0.0f;
        for (PartyMemberFacts const& m : world.party)
            if (!m.isPet && m.alive && !(m.isBot && m.role == ROLE_HEALER))
                estimate += FALLBACK_DPS_PER_LEVEL * m.level;
        return std::max(1.0f, estimate);
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
        return ContributionPerSecAt(world, member, world.rates.incomingDps);
    }

    float ContributionPerSecAt(WorldModel const& world, PartyMemberFacts const& member, float incomingDps)
    {
        if (!member.alive)
            return 0.0f;

        if (member.isBot && member.role == ROLE_HEALER)
            return Points(world, incomingDps) / float(std::max(1u, world.rates.healers));

        // One second of this member's damage shortens the fight by share seconds,
        // which saves that much incoming damage.
        float const share = 1.0f / float(std::max(1u, world.rates.damageDealers));
        return Points(world, incomingDps) * share;
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
            if (!e.crowdControlled && e.victim == member)
                total += HitDps(e, *m);
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
}
}
