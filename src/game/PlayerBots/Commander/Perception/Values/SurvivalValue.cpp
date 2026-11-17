#include "Commander/Perception/Values/SurvivalValue.h"
#include "Commander/Perception/Values/ControlValue.h"
#include "Commander/Perception/Values/ManaWorth.h"
#include "Commander/Perception/Spatial/PackResolver.h"
#include "Commander/Perception/Tanks.h"
#include "Commander/Perception/Forecast/ThreatView.h"
#include "Commander/Perception/Values/ValueModel.h"
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
        d.worstTtd = ThreatView::NO_LIMIT;
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

        HealIn const in = HealOn(world, member, d.dps);
        d.heal = in.hps;
        d.healDelaySec = in.delaySec;
        d.timeline = IncomingTimeline::Of(world, member);
        if (d.dps > 0.0f || !d.timeline.events.empty())
        {
            float const health = float(member.health) + member.absorb;
            d.pendingHeal = PendingHealOn(world, member, health, d.dps);
            for (PartyMemberFacts const& h : world.party)
                if (h.pendingHeal.target == member.guid && h.pendingHeal.amount > 0.0f)
                    d.timeline.AddHeal(h.pendingHeal.landMs / 1000.0f, h.pendingHeal.amount, h.pendingHeal.spellId, h.guid);
            d.timeline.AddHots(member);
            d.ttd = d.timeline.TimeToLose(health, false, d.Healers());
            d.worstTtd = d.timeline.TimeToLose(health, true, d.Healers());
        }
        return d;
    }

    float PendingHealOn(WorldModel const& world, PartyMemberFacts const& member, float health, float dps)
    {
        float total = 0.0f;
        for (PartyMemberFacts const& h : world.party)
        {
            PendingHeal const& p = h.pendingHeal;
            if (p.target != member.guid || p.amount <= 0.0f)
                continue;
            float const left = health - std::max(0.0f, dps) * p.landMs / 1000.0f;
            if (left <= 0.0f)
                continue;
            total += std::min(p.amount, std::max(0.0f, float(member.maxHealth) - left));
        }
        return total;
    }

    float Demand(WorldModel const& world, PartyMemberFacts const& m)
    {
        if (!m.alive || m.isPet)
            return 0.0f;
        return std::max(Value::IncomingOn(world, m.guid) * m.damageTakenMult, m.takenDps);
    }

    HealIn HealOn(WorldModel const& world, PartyMemberFacts const& member, float demand)
    {
        HealIn in;
        in.delaySec = ThreatView::NO_LIMIT;
        for (PartyMemberFacts const& h : world.party)
        {
            if (!h.isBot || h.isPet || !h.CanAct() || h.healRates.empty() || !h.mana ||
                PackResolver::Distance(h.pos, member.pos) > HEAL_RANGE_YD)
                continue;
            HealRate const* best = nullptr;
            for (HealRate const& r : h.healRates)
                if (!best || r.hps > best->hps)
                    best = &r;
            // One healer, several members being hit: its heals go round by the damage each takes.
            float others = 0.0f;
            for (PartyMemberFacts const& o : world.party)
                if (o.guid != member.guid && PackResolver::Distance(h.pos, o.pos) <= HEAL_RANGE_YD)
                    others += Demand(world, o);
            float const share = demand + others > 0.0f ? demand / (demand + others) : 1.0f;
            in.hps += best->hps * share;
            // The first heal lands after its cast (and after the one it is casting now).
            float const delay = best->castSec + (h.casting ? h.castRemainingMs / 1000.0f : 0.0f);
            in.delaySec = std::min(in.delaySec, delay);
        }
        if (in.hps <= 0.0f)
            in.delaySec = 0.0f;
        return in;
    }

    float TimeToDieUnder(float health, float dps, HealIn const& heal)
    {
        if (dps <= 0.0f)
            return ThreatView::NO_LIMIT;
        return TimeToDie(health, dps, heal.delaySec, dps - heal.hps);
    }

    RescueValue::Option Save(WorldModel const& world, PartyMemberFacts const& member, Danger const& danger,
                             PartyMemberFacts const& caster, SurvivalAbility const& a)
    {
        RescueValue::Option o;
        // `ready` covers the caster being able to act (a controlled one only for a spell that lifts it).
        if (!a.ready)
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
        // A consumable only when the fight is not over before it would die (user 2026-11-15).
        if (a.itemId && Value::FightRemainingSec(world) <= danger.ttd)
            return o;

        float const dps = danger.dps;
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
            case SurvivalAbility::Effect::ReducePhysical:
                factor = dps > 0.0f ? std::max(0.0f, 1.0f - danger.physicalDps / dps * (1.0f - a.amount)) : 1.0f;
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

        // The same timeline as danger.ttd (pending heals in it), the hits x factor during sec.
        IncomingTimeline::Relief relief = danger.Healers();
        relief.factor = factor;
        relief.factorSec = sec;
        float const ttd = danger.timeline.TimeToLose(float(member.health) + member.absorb + extra, false, relief);
        float kept = dps * (1.0f - factor) * sec + healUsed;
        if (a.effect == SurvivalAbility::Effect::Absorb || a.effect == SurvivalAbility::Effect::ExtraHealth)
            kept += std::min(extra, dps * HORIZON_SEC);
        o.benefit = Value::DeathPenalty(member) * (Risk(danger.ttd) - Risk(ttd)) + Value::Points(world, kept);

        o.cost = (a.gcd ? Value::LostAction(world, caster, Value::GCD_SEC) : 0.0f) +
                 ManaWorth::Points(world, caster, float(a.manaCost));
        if (a.casterStops)
            o.cost += Value::LostAction(world, caster, a.durationSec);
        if (a.targetStops && !self && (member.role == ROLE_MELEE_DPS || member.role == ROLE_TANK))
            o.cost += Value::LostAction(world, member, a.durationSec);
        o.cost += Value::CooldownPoints(a.cooldownMs / 1000.0f, Value::FightRemainingSec(world) + NEXT_FIGHTS_SEC);
        // A heal item on the cooldown of a held mana item (potions; healthstones and mana gems):
        // that mana is gone for this fight (user 2026-11-17: a healer's mana potion is worth
        // more than its healing potion). ManaWorth: what the mana would do, times its scarcity.
        if (a.itemCategory)
            for (ManaItemAbility const& m : caster.manaItems)
                if (m.category == a.itemCategory)
                    o.cost += ManaWorth::Points(world, caster, m.mana);
        o.cost += Value::ResetCost(world, caster, a.reset, Value::FightRemainingSec(world) + NEXT_FIGHTS_SEC);
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
