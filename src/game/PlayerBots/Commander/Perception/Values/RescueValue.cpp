#include "Commander/Perception/Values/RescueValue.h"
#include "Commander/Perception/Values/ControlValue.h"
#include "Commander/Perception/Values/LooseEnemy.h"
#include "Commander/Perception/Values/ManaWorth.h"
#include "Commander/Perception/Values/SwitchCost.h"
#include "Commander/Perception/Spatial/PackResolver.h"
#include "Commander/Perception/Tanks.h"
#include "Commander/Perception/Forecast/ThreatView.h"
#include "Commander/Perception/Values/ValueModel.h"
#include "SpellEntry.h"
#include <algorithm>

namespace Commander
{
namespace RescueValue
{
    ThreatEntry const* NextHolder(EnemyFacts const& e, ObjectGuid who, float threatAfter)
    {
        ThreatEntry const* next = nullptr;
        for (ThreatEntry const& r : e.threat)
        {
            if (r.guid == who || !r.onList)
                continue;
            if (r.threat > threatAfter * (r.inMeleeReach ? ThreatView::MELEE_AGGRO_FACTOR : ThreatView::RANGED_AGGRO_FACTOR) &&
                (!next || r.threat > next->threat))
                next = &r;
        }
        return next;
    }

    PartyMemberFacts const* LooseVictim(WorldModel const& world, EnemyFacts const& e)
    {
        if (!e.acting || e.crowdControlled || e.onAllyOnly || !e.health || e.victim.IsEmpty())
            return nullptr;
        // On an ally (tank-like or not): it fights it on purpose (combat scenario 9).
        if (world.FindAlly(e.victim))
            return nullptr;
        PartyMemberFacts const* victim = world.FindMember(e.victim);
        if (!victim || victim->charmed || Tanks::IsTank(*victim))
            return nullptr;
        // A pet that growls it back: taking it from the pet is a war the tank loses.
        if (victim->isPet && victim->tauntsItself)
            return nullptr;
        return victim;
    }

    float TemporaryTauntSec(SpellEntry const* spell)
    {
        bool aura = false;
        for (uint32 i = 0; i < MAX_SPELL_EFFECTS; ++i)
        {
            if (spell->Effect[i] == SPELL_EFFECT_ATTACK_ME)
                return 0.0f;
            if (spell->Effect[i] == SPELL_EFFECT_APPLY_AURA && spell->EffectApplyAuraName[i] == SPELL_AURA_MOD_TAUNT)
                aura = true;
        }
        return aura ? std::max(0, spell->GetDuration()) / 1000.0f : 0.0f;
    }

    float TauntSecOnVictim(float gap, float tankRate, float holdSec, float horizon)
    {
        float sec = std::min(TAUNT_REACTION_SEC, horizon);
        if (holdSec > 0.0f && holdSec < horizon)
        {
            float const rest = std::max(0.0f, gap - tankRate * holdSec);
            float const recovery = tankRate > 1.0f ? rest / tankRate : ThreatView::NO_LIMIT;
            sec += std::min(recovery, horizon - holdSec);
        }
        return sec;
    }

    float EnemyDps(WorldModel const& world)
    {
        float dps = 0.0f;
        for (EnemyFacts const& e : world.enemies)
            if (!e.crowdControlled && e.acting && !e.onAllyOnly)
                dps += e.meleeDps + e.rangedDps;
        return dps;
    }

    float NextTauntDps(WorldModel const& world, ObjectGuid except)
    {
        float dps = 0.0f;
        for (EnemyFacts const& e : world.enemies)
            if (e.guid != except && !e.crowdControlled && e.acting && !e.onAllyOnly)
                dps = std::max(dps, e.meleeDps + e.rangedDps);
        return dps;
    }

    Option Taunt(WorldModel const& world, EnemyFacts const& e, PartyMemberFacts const& victim,
                 PartyMemberFacts const& tank, TauntAbility const& taunt, float tankRate, float distance,
                 float enemyDps)
    {
        Option o;
        bool immune = false;
        float const mult = LooseEnemy::Mult(e, &immune);
        if (immune || !taunt.ready || !e.InSightOf(tank.guid))
            return o;
        LooseValue const v = LooseEnemy::WinBack(world, e, victim, tank, tankRate, distance);
        float const walkSec = std::max(0.0f, distance - e.boundingRadius - taunt.maxRange) / RUN_YARDS_PER_SEC;
        float const onVictim = std::min(v.horizon, walkSec + TauntSecOnVictim(v.gap, tankRate, taunt.holdSec, v.horizon));
        o.benefit = (v.lossNothing - LooseEnemy::Loss(world, e, victim, tank, onVictim)) * mult;
        float const share = std::min(1.0f, (taunt.cooldownMs / 1000.0f) / RESERVE_HORIZON_SEC);
        o.cost = Value::LostAction(world, tank, Value::GCD_SEC + walkSec) +
                 share * Value::Points(world, enemyDps * TAUNT_RESERVE_SEC);
        if (taunt.stanceSwap)
            o.cost += Value::LostAction(world, tank, STANCE_SWAP_COST_SEC);
        o.urgent = v.deadly;
        return o;
    }

    Option Threat(WorldModel const& world, EnemyFacts const& e, PartyMemberFacts const& victim,
                  PartyMemberFacts const& tank, float tankRate, float distance)
    {
        Option o;
        LooseValue const v = LooseEnemy::WinBack(world, e, victim, tank, tankRate, distance);
        o.benefit = (v.lossNothing - v.lossThreat) * LooseEnemy::Mult(e, nullptr);
        o.cost = tank.victim == e.guid ? 0.0f : SwitchCost::Of(world, tank, e, MELEE_REACH);
        o.urgent = v.deadly;
        return o;
    }

    namespace
    {
        // Vanish (ThreatDropAbility::wipe): every enemy on the victim turns away at once.
        Option Wipe(WorldModel const& world, PartyMemberFacts const& victim, ThreatDropAbility const& a)
        {
            Option o;
            for (EnemyFacts const& other : world.enemies)
            {
                if (!other.acting || other.crowdControlled || other.victim != victim.guid)
                    continue;
                float const seconds = std::min(HORIZON_SEC, Value::EnemyTimeToDie(world, other));
                ThreatEntry const* next = NextHolder(other, victim.guid, 0.0f);
                PartyMemberFacts const* holder = next ? world.FindMember(next->guid) : nullptr;
                if (holder)
                    o.benefit += std::max(0.0f, Value::MoveLoss(world, other, victim, *holder, seconds));
                else if (!next)
                    o.benefit += Value::HitLoss(world, other, victim, seconds);     // it leaves the fight
            }
            o.cost = Value::LostAction(world, victim, Value::GCD_SEC);
            float const cooldownSec = a.cooldownMs / 1000.0f;
            if (cooldownSec > Value::GCD_SEC)
                o.cost += ControlValue::COOLDOWN_POINTS_PER_MIN *
                          std::min(cooldownSec, Value::FightRemainingSec(world) + WIPE_NEXT_FIGHTS_SEC) / 60.0f;
            float const incoming = Value::IncomingOn(world, victim.guid);
            o.urgent = incoming > 0.0f && float(victim.health) / incoming < FADE_URGENT_SEC;
            return o;
        }
    }

    Option ThreatDrop(WorldModel const& world, EnemyFacts const& e, PartyMemberFacts const& victim,
                      ThreatDropAbility const& a)
    {
        Option o;
        if (!a.spellId || !a.ready)
            return o;
        if (a.wipe)
            return Wipe(world, victim, a);
        if (a.drop <= 0.0f)
            return o;
        if (a.onTarget)
        {
            float const reach = PackResolver::Distance(victim.pos, e.pos) - e.boundingRadius;
            if (reach > a.maxRange || !e.InSightOf(victim.guid))
                return o;
        }
        else if (a.durationSec <= 0.0f)
            return o;
        float mine = 0.0f;
        for (ThreatEntry const& r : e.threat)
            if (r.guid == victim.guid)
                mine = r.threat;
        ThreatEntry const* next = NextHolder(e, victim.guid, std::max(0.0f, mine - a.drop));
        if (!next)
            return o;
        // For good (Feint, Cower): the victim's own stop rule (ThreatRoom) keeps it from taking
        // the enemy back, so it counts like a taunt's horizon.
        float const lasts = a.durationSec > 0.0f ? a.durationSec : HORIZON_SEC;
        float const seconds = std::min(lasts, Value::EnemyTimeToDie(world, e));
        if (PartyMemberFacts const* holder = world.FindMember(next->guid))
            o.benefit = std::max(0.0f, Value::MoveLoss(world, e, victim, *holder, seconds));
        else
            o.benefit = Value::HitLoss(world, e, victim, seconds);

        o.cost = Value::LostAction(world, victim, Value::GCD_SEC) + ManaWorth::Points(world, victim, float(a.manaCost));
        float const cooldownSec = a.cooldownMs / 1000.0f;
        if (cooldownSec > Value::GCD_SEC)
            o.cost += ControlValue::COOLDOWN_POINTS_PER_MIN * std::min(cooldownSec, Value::FightRemainingSec(world)) / 60.0f;

        float const incoming = Value::IncomingOn(world, victim.guid);
        o.urgent = incoming > 0.0f && float(victim.health) / incoming < FADE_URGENT_SEC;
        return o;
    }
}
}
