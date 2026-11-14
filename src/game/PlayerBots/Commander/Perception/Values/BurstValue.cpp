#include "Commander/Perception/Values/BurstValue.h"
#include "Commander/Perception/Values/SurvivalValue.h"
#include "Commander/Perception/Values/ValueModel.h"
#include "Commander/Perception/Forecast/ThreatRoom.h"
#include "Commander/Perception/Forecast/ThreatView.h"
#include "Commander/Perception/Spatial/PackResolver.h"
#include "Commander/Perception/Tanks.h"
#include <algorithm>

namespace Commander
{
namespace BurstValue
{
    namespace
    {
        bool ThreatAllows(WorldModel const& world, PartyMemberFacts const& member, BurstAbility const& a,
                          EnemyFacts const& victim, bool noHold, float tickSec)
        {
            if (noHold || Tanks::IsTank(member) || victim.victim.IsEmpty() || victim.victim == member.guid)
                return true;
            bool holder = false;
            if (PartyMemberFacts const* v = world.FindMember(victim.victim))
                holder = Tanks::IsHolder(*v);
            else if (AllyFacts const* ally = world.FindAlly(victim.victim))
                holder = ally->tankLike;
            if (!holder)
                return true;
            return ThreatView::Headroom(victim, member.guid) >=
                   ThreatRoom::Enter(world, member.guid, victim, tickSec) * (1.0f + a.damageMult);
        }

        uint32 EnemiesNear(WorldModel const& world, PartyMemberFacts const& member, bool onSelf)
        {
            uint32 n = 0;
            for (EnemyFacts const& e : world.enemies)
            {
                if (!e.acting || e.crowdControlled)
                    continue;
                if (onSelf && (e.victim != member.guid || !e.meleeEnabled))
                    continue;
                if (PackResolver::Distance(member.pos, e.pos) - e.boundingRadius <= NEAR_YD)
                    ++n;
            }
            return n;
        }

        bool PetOn(WorldModel const& world, PartyMemberFacts const& member, ObjectGuid victim)
        {
            for (PartyMemberFacts const& p : world.party)
                if (p.isPet && p.ownerGuid == member.guid && p.alive && p.victim == victim)
                    return true;
            return false;
        }
    }

    bool BigFight(WorldModel const& world)
    {
        for (EnemyFacts const& e : world.enemies)
            if (e.boss && e.acting)
                return true;
        for (PartyMemberFacts const& m : world.party)
            if (!m.isPet && m.alive && SurvivalValue::Of(world, m).InDanger())
                return true;
        return false;
    }

    Option Of(WorldModel const& world, PartyMemberFacts const& member, BurstAbility const& a,
              EnemyFacts const* victim, bool noHold, float tickSec)
    {
        Option o;
        if (!a.ready)
        {
            o.why = "not ready";
            return o;
        }
        if (!victim || victim->health == 0)
        {
            o.why = "no victim";
            return o;
        }
        if (!ThreatAllows(world, member, a, *victim, noHold, tickSec))
        {
            o.why = "threat: too close to the tank";
            return o;
        }
        float const fightLeft = Value::FightRemainingSec(world);
        if (fightLeft < a.durationSec * MIN_LAST_SHARE && a.thenSpellId == 0)
        {
            o.why = "fight about to end";
            return o;
        }
        if (a.cooldownMs / 1000.0f > SHORT_COOLDOWN_SEC)
        {
            if (!BigFight(world))
            {
                o.why = "long cooldown: kept for a boss or an emergency";
                return o;
            }
            bool danger = false;
            for (PartyMemberFacts const& m : world.party)
                if (!m.isPet && m.alive && SurvivalValue::Of(world, m).InDanger())
                    danger = true;
            if (!danger && fightLeft < LONG_MIN_FIGHT_SEC)
            {
                o.why = "long cooldown: fight too short";
                return o;
            }
        }
        if (a.notTank && Tanks::IsTank(member))
        {
            o.why = "not on a tank";
            return o;
        }
        if (a.healTakenCut && Value::TakenDps(world, member.guid) > 0.0f)
        {
            o.why = "hit: healing taken would be halved";
            return o;
        }
        if (a.minManaPct > 0.0f && member.manaPct >= 0.0f && member.manaPct < a.minManaPct)
        {
            o.why = "mana too low";
            return o;
        }
        if (a.minEnemiesNear && EnemiesNear(world, member, false) < a.minEnemiesNear)
        {
            o.why = "too few enemies around";
            return o;
        }
        if (a.minAttackersOnSelf && EnemiesNear(world, member, true) < a.minAttackersOnSelf)
        {
            o.why = "too few enemies hitting it";
            return o;
        }
        if (a.needsPet && !PetOn(world, member, victim->guid))
        {
            o.why = "pet not on the victim";
            return o;
        }
        if (a.needsCombo && (member.comboTarget != victim->guid || member.comboPoints < FULL_COMBO))
        {
            o.why = "waiting for full combo points";
            return o;
        }

        float const sec = std::min(a.durationSec, fightLeft);
        float const perSec = Value::ContributionPerSec(world, member);
        o.benefit = perSec * (a.damageMult * sec + a.extraSec);
        if (a.gcd)
            o.cost += Value::LostAction(world, member, Value::GCD_SEC);
        if (a.damageTakenUp > 0.0f)
            o.cost += Value::Points(world, Value::TakenDps(world, member.guid) * a.damageTakenUp * sec);
        o.ok = true;
        o.why = a.thenSpellId ? "pairing: boost the next cast" : "burst";
        return o;
    }
}
}
