#include "Commander/Perception/SurvivalValue.h"
#include "Commander/Perception/ControlValue.h"
#include "Commander/Perception/ManaWorth.h"
#include "Commander/Perception/PackResolver.h"
#include "Commander/Perception/Tanks.h"
#include "Commander/Perception/ThreatView.h"
#include "Commander/Perception/ValueModel.h"
#include <algorithm>
#include <cmath>

namespace Commander
{
namespace SurvivalValue
{
    namespace
    {
        // Seconds `health` lasts losing `drain1` per second for `sec`, then `drain0`.
        float TimeToDie(float health, float drain1, float sec, float drain0)
        {
            if (drain1 > 0.0f && health <= drain1 * sec)
                return health / drain1;
            float const left = health - std::max(0.0f, drain1) * sec;
            return drain0 > 0.0f ? sec + left / drain0 : ThreatView::NO_LIMIT;
        }
    }

    float Risk(float ttd)
    {
        if (!(ttd < HORIZON_SEC))
            return 0.0f;
        return std::min(1.0f, std::max(0.0f, 1.0f - ttd / HORIZON_SEC));
    }

    Danger Of(WorldModel const& world, PartyMemberFacts const& member)
    {
        Danger d;
        d.ttd = ThreatView::NO_LIMIT;
        if (!member.alive || (member.immunePhysical && member.immuneMagic))
            return d;
        for (EnemyFacts const& e : world.enemies)
        {
            if (!e.acting || e.crowdControlled || e.victim != member.guid)
                continue;
            if (e.meleeEnabled)
                d.physicalDps += Value::HitDps(e, member);
        }
        d.enemyDps = Value::IncomingOn(world, member.guid) * member.damageTakenMult;
        d.physicalDps *= member.damageTakenMult;
        d.dps = std::max(d.enemyDps, member.takenDps);
        if (member.immunePhysical)
            d.dps = std::max(0.0f, d.dps - d.physicalDps);

        for (PartyMemberFacts const& h : world.party)
        {
            if (!h.isBot || h.isPet || !h.CanAct() || h.healRates.empty() || !h.mana ||
                PackResolver::Distance(h.pos, member.pos) > HEAL_RANGE_YD)
                continue;
            float best = 0.0f;
            for (HealRate const& r : h.healRates)
                best = std::max(best, r.hps);
            d.heal += best;
        }
        float const drain = d.dps - d.heal;
        if (drain > 0.0f)
            d.ttd = (float(member.health) + member.absorb) / drain;
        return d;
    }

    RescueValue::Option Save(WorldModel const& world, PartyMemberFacts const& member, Danger const& danger,
                             PartyMemberFacts const& caster, SurvivalAbility const& a)
    {
        RescueValue::Option o;
        if (!a.ready || !caster.CanAct())
            return o;
        bool const self = caster.guid == member.guid;
        if (a.selfOnly && !self)
            return o;
        if (!self && PackResolver::Distance(caster.pos, member.pos) > a.maxRange)
            return o;
        if (a.notOnTank && Tanks::IsTank(member))
            return o;
        if (a.mechanic && a.mechanic < 32 && (member.immuneMechanics & (1u << a.mechanic)))
            return o;

        float const dps = danger.dps;
        float const heal = danger.heal;
        float factor = 1.0f;
        float sec = std::min(a.durationSec, HORIZON_SEC);
        float extra = 0.0f;         // health added (absorb, Last Stand, a heal)
        float healUsed = 0.0f;
        switch (a.effect)
        {
            case SurvivalAbility::Effect::Absorb:
                extra = a.amount;
                sec = 0.0f;
                break;
            case SurvivalAbility::Effect::Reduce:
                factor = a.amount;
                break;
            case SurvivalAbility::Effect::Immune:
                factor = 0.0f;
                break;
            case SurvivalAbility::Effect::ImmunePhysical:
                factor = dps > 0.0f ? std::max(0.0f, 1.0f - danger.physicalDps / dps) : 1.0f;
                break;
            case SurvivalAbility::Effect::Heal:
                healUsed = std::min(a.amount, float(member.maxHealth) - float(member.health));
                extra = healUsed;
                sec = 0.0f;
                break;
            case SurvivalAbility::Effect::ExtraHealth:
                extra = a.amount;
                sec = 0.0f;
                break;
            case SurvivalAbility::Effect::DropThreat:
                factor = dps > 0.0f ? std::max(0.0f, 1.0f - danger.enemyDps / dps) : 1.0f;
                sec = HORIZON_SEC;
                break;
        }

        float const health = float(member.health) + member.absorb + extra;
        float const ttd = TimeToDie(health, dps * factor - heal, sec, dps - heal);
        float kept = dps * (1.0f - factor) * sec + healUsed;
        if (a.effect == SurvivalAbility::Effect::Absorb || a.effect == SurvivalAbility::Effect::ExtraHealth)
            kept += std::min(extra, dps * HORIZON_SEC);
        o.benefit = Value::DeathPenalty(member) * (Risk(danger.ttd) - Risk(ttd)) + Value::Points(world, kept);

        o.cost = Value::LostAction(world, caster, Value::GCD_SEC) + ManaWorth::Points(world, caster, float(a.manaCost));
        if (a.casterStops)
            o.cost += Value::LostAction(world, caster, a.durationSec);
        if (a.targetStops && !self && (member.role == ROLE_MELEE_DPS || member.role == ROLE_TANK))
            o.cost += Value::LostAction(world, member, a.durationSec);
        float const cooldownSec = a.cooldownMs / 1000.0f;
        if (cooldownSec > Value::GCD_SEC)
            o.cost += ControlValue::COOLDOWN_POINTS_PER_MIN *
                      std::min(cooldownSec, Value::FightRemainingSec(world) + NEXT_FIGHTS_SEC) / 60.0f;
        if (a.effect == SurvivalAbility::Effect::DropThreat)
        {
            for (EnemyFacts const& e : world.enemies)
            {
                if (!e.acting || e.crowdControlled || e.victim != member.guid)
                    continue;
                ThreatEntry const* next = RescueValue::NextHolder(e, member.guid, 0.0f);
                PartyMemberFacts const* holder = next ? world.FindMember(next->guid) : nullptr;
                if (holder)
                    o.cost += std::max(0.0f, Value::MoveLoss(world, e, *holder, member,
                        std::min(HORIZON_SEC, Value::EnemyTimeToDie(world, e))));
            }
        }
        o.urgent = true;
        return o;
    }
}
}
