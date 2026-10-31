#include "Commander/Behaviors/ThreatBehavior.h"
#include "Commander/Perception/ThreatView.h"
#include "Commander/Perception/PackResolver.h"
#include "Commander/Perception/ValueModel.h"
#include "Commander/Perception/ProfileStore.h"
#include "Commander/CommanderMgr.h"
#include "PartyBotAI.h"
#include "SpellEntry.h"
#include "Timer.h"
#include <algorithm>
#include <cmath>

namespace Commander
{
    namespace
    {
        Proposal MakeProposal(ProposalTier tier, BotActionType type, ObjectGuid target = ObjectGuid(),
                              SpellEntry const* spell = nullptr)
        {
            Proposal p;
            p.tier = tier;
            p.source = "threat";
            p.action.type = type;
            p.action.target = target;
            p.action.spell = spell;
            return p;
        }

        // Somebody whose job is holding enemies: a tank bot, or the player (may be tanking).
        bool IsHolder(PartyMemberFacts const& m)
        {
            if (m.isPet)
                return false;
            return !m.isBot || m.role == ROLE_TANK;
        }

        // Direct heal effects of the spell with the healer's and target's healing bonuses.
        float EstimateDirectHeal(Player* healer, Unit* target, SpellEntry const* spell)
        {
            float total = 0.0f;
            for (uint32 i = 0; i < MAX_SPELL_EFFECTS; ++i)
            {
                if (spell->Effect[i] != SPELL_EFFECT_HEAL)
                    continue;
                SpellEffectIndex const idx = SpellEffectIndex(i);
                float value = float(spell->CalculateSimpleValue(idx));
                value = healer->SpellHealingBonusDone(target, spell, idx, value, SPELL_DIRECT_DAMAGE);
                value = target->SpellHealingBonusTaken(healer, spell, idx, value, SPELL_DIRECT_DAMAGE);
                total += value;
            }
            return total;
        }

        // Same base-point measure CombatBotBaseAI::SelectMostEfficientHealingSpell uses.
        int32 BasePoints(SpellEntry const* spell)
        {
            int32 points = 0;
            for (uint32 i = 0; i < MAX_SPELL_EFFECTS; ++i)
                if (spell->Effect[i] == SPELL_EFFECT_HEAL)
                    points += spell->EffectBasePoints[i];
            return points;
        }

        float TauntMult(EnemyFacts const& e, bool* immune)
        {
            std::shared_ptr<CreatureProfile const> p = sProfileStore.GetCreature(e.entry);
            *immune = p && p->tauntImmune;
            return (p && p->hasOverride) ? p->override.tauntMult : 1.0f;
        }
    }

    bool ThreatBehavior::AppliesTo(PartyBotAI const* bot) const
    {
        switch (bot->GetRole())
        {
            case ROLE_TANK:
            case ROLE_MELEE_DPS:
            case ROLE_RANGE_DPS:
            case ROLE_HEALER:
                return true;
            default:
                return false;
        }
    }

    bool ThreatBehavior::ControlsTaunt(PartyBotAI const* bot) const
    {
        return bot->GetRole() == ROLE_TANK && !bot->m_spellListTaunt.empty();
    }

    void ThreatBehavior::Propose(PartyBotAI const* bot, WorldModel const& world,
                                 TeamPlan const& /*plan*/, std::vector<Proposal>& out) const
    {
        if (world.enemies.empty())
            return;

        switch (bot->GetRole())
        {
            case ROLE_TANK:
                ProposeTank(bot, world, out);
                break;
            case ROLE_MELEE_DPS:
            case ROLE_RANGE_DPS:
                ProposeDamage(bot, world, out);
                break;
            case ROLE_HEALER:
                ProposeHealer(bot, world, out);
                break;
            default:
                break;
        }
    }

    // Tank: threat-behavior doc section 3, value-scoring doc 5.3.
    void ThreatBehavior::ProposeTank(PartyBotAI const* bot, WorldModel const& world, std::vector<Proposal>& out) const
    {
        Player* me = bot->me;
        ObjectGuid const self = me->GetObjectGuid();
        Unit const* pVictim = me->GetVictim();
        ObjectGuid const myTarget = pVictim ? pVictim->GetObjectGuid() : ObjectGuid();
        PartyMemberFacts const* myFacts = world.FindMember(self);
        if (!myFacts)
            return;

        // What we can generate: best observed threat rate on any enemy.
        float tankRate = 0.0f;
        for (EnemyFacts const& e : world.enemies)
            if (ThreatEntry const* t = ThreatView::FindEntry(e, self))
                tankRate = std::max(tankRate, t->threatPerSec);

        // A taunt used now is not there for the next enemy that breaks loose.
        float enemyMelee = 0.0f;
        for (EnemyFacts const& e : world.enemies)
            if (!e.crowdControlled)
                enemyMelee += e.meleeDps;
        float const switchCost = Value::LostAction(world, *myFacts, Value::GCD_SEC);

        // 1. Loose enemies hitting someone who is not a tank.
        for (EnemyFacts const& e : world.enemies)
        {
            if (e.crowdControlled || e.victim.IsEmpty() || e.victim == self)
                continue;
            PartyMemberFacts const* victim = world.FindMember(e.victim);
            if (!victim || victim->isPet)
                continue;   // not one of us, or a pet (pets can take a few hits)
            if (victim->isBot && victim->role == ROLE_TANK)
                continue;   // another tank holds it

            bool tauntImmune = false;
            float const mult = TauntMult(e, &tauntImmune);

            float const gap = std::max(0.0f, ThreatView::Headroom(e, self));
            float const recovery = tankRate > 1.0f ? gap / tankRate : ThreatView::NO_LIMIT;
            float const horizon = std::min(LOOSE_HORIZON_SEC, Value::EnemyTimeToDie(world, e));

            float const lossNothing = Value::HitLoss(world, e, *victim, horizon);
            float const lossThreat = Value::HitLoss(world, e, *victim, std::min(recovery, horizon));
            float const lossTaunt = Value::HitLoss(world, e, *victim, std::min(TAUNT_REACTION_SEC, horizon));
            bool const deadly = Value::SurviveSec(e, *victim) < horizon;
            ProposalTier const tier = deadly ? ProposalTier::Urgent : ProposalTier::Assigned;

            if (!tauntImmune)
            {
                for (SpellEntry const* pTaunt : bot->m_spellListTaunt)
                {
                    float const share = std::min(1.0f, (pTaunt->RecoveryTime / 1000.0f) / LOOSE_HORIZON_SEC);
                    float const reserve = share * Value::Points(world, enemyMelee * TAUNT_RESERVE_SEC);
                    Proposal p = MakeProposal(tier, BotActionType::Cast, e.guid, pTaunt);
                    p.reason = "taunt loose enemy";
                    p.SetValue((lossNothing - lossTaunt) * mult, switchCost + reserve);
                    out.push_back(p);
                }
            }

            // Win it back with threat (also the fallback when the taunt is on cooldown).
            // If it already is our target, the rotation builds threat on it.
            if (e.guid != myTarget)
            {
                Proposal p = MakeProposal(tier, BotActionType::Attack, e.guid);
                p.reason = "win back with threat";
                p.SetValue((lossNothing - lossThreat) * mult, switchCost);
                out.push_back(p);
            }
        }

        // 2. Top up the enemy we hold with the smallest lead (not valued yet).
        EnemyFacts const* weakest = nullptr;
        float weakestRatio = ThreatView::NO_LIMIT;
        float currentRatio = ThreatView::NO_LIMIT;
        for (EnemyFacts const& e : world.enemies)
        {
            if (e.crowdControlled || e.victim != self)
                continue;
            float const ratio = ThreatView::VictimLead(e) / std::max(1.0f, e.victimThreat);
            if (e.guid == myTarget)
                currentRatio = ratio;
            else if (ratio < weakestRatio)
            {
                weakestRatio = ratio;
                weakest = &e;
            }
        }
        if (weakest && weakestRatio < TANK_LEAD_SWITCH && currentRatio > TANK_LEAD_KEEP)
        {
            Proposal p = MakeProposal(ProposalTier::Assigned, BotActionType::Attack, weakest->guid);
            p.score = 1.0f - weakestRatio;
            p.reason = "top up smallest lead";
            out.push_back(p);
        }
    }

    // Damage dealers: stop before pulling aggro off a holder, when it is worth it.
    void ThreatBehavior::ProposeDamage(PartyBotAI const* bot, WorldModel const& world, std::vector<Proposal>& out) const
    {
        Player* me = bot->me;
        Unit const* pVictim = me->GetVictim();
        if (!pVictim)
            return;

        ObjectGuid const self = me->GetObjectGuid();
        PartyMemberFacts const* myFacts = world.FindMember(self);
        EnemyFacts const* e = world.FindEnemy(pVictim->GetObjectGuid());
        if (!myFacts || !e || e->victim.IsEmpty() || e->victim == self)
            return;

        // Only protect a holder. If it is on a healer or another dps, taking it is not worse.
        PartyMemberFacts const* holder = world.FindMember(e->victim);
        if (!holder || !IsHolder(*holder))
            return;

        if (ThreatView::Headroom(*e, self) >= DPS_STOP_HEADROOM * e->victimThreat)
            return;

        // What pulling it costs: its hits on me until the tank has it back. Weak enemies on
        // plate, or enemies about to die, cost little.
        float const ttd = Value::EnemyTimeToDie(world, *e);
        float const avoided = Value::HitLoss(world, *e, *myFacts, std::min(TANK_RECLAIM_SEC, ttd));
        float const tickSec = sCommanderMgr.GetBotUpdateInterval() / 1000.0f;

        Position3 myPos;
        myPos.x = me->GetPositionX();
        myPos.y = me->GetPositionY();
        myPos.z = me->GetPositionZ();

        EnemyFacts const* best = nullptr;
        float bestRatio = 0.0f;
        for (EnemyFacts const& a : world.enemies)
        {
            if (a.guid == e->guid || a.crowdControlled || a.victim.IsEmpty() || a.victim == self || a.victimThreat <= 0.0f)
                continue;
            if (PackResolver::Distance(myPos, a.pos) > DPS_SWITCH_RANGE)
                continue;
            float const ratio = ThreatView::Headroom(a, self) / a.victimThreat;
            if (ratio >= DPS_SWITCH_HEADROOM && ratio > bestRatio)
            {
                bestRatio = ratio;
                best = &a;
            }
        }

        if (best)
        {
            Proposal p = MakeProposal(ProposalTier::Assigned, BotActionType::Attack, best->guid);
            p.reason = "near aggro, switch";
            p.SetValue(avoided, Value::LostAction(world, *myFacts, SWITCH_TARGET_SEC));
            out.push_back(p);
        }
        Proposal p = MakeProposal(ProposalTier::Assigned, BotActionType::HoldFire);
        p.reason = "near aggro, stop";
        p.SetValue(avoided, Value::LostAction(world, *myFacts, tickSec));
        out.push_back(p);
    }

    // Healers: do not pull aggro with a heal unless the target needs it now.
    void ThreatBehavior::ProposeHealer(PartyBotAI const* bot, WorldModel const& world, std::vector<Proposal>& out) const
    {
        Player* me = bot->me;
        Unit* pTarget = bot->SelectHealTarget();     // same call as FindAndHealInjuredAlly()
        if (!pTarget)
            return;
        if (pTarget->GetHealthPercent() < HEAL_IGNORE_THREAT_PCT)
            return;

        // The rotation would put a HoT on it first (CombatBotBaseAI::HealInjuredTarget); HoT threat comes in ticks.
        if (pTarget->GetHealthPercent() >= 80.0f && !pTarget->HasAuraType(SPELL_AURA_PERIODIC_HEAL) &&
            !bot->m_spellListPeriodicHeal.empty())
            return;

        ObjectGuid const self = me->GetObjectGuid();
        PartyMemberFacts const* myFacts = world.FindMember(self);
        PartyMemberFacts const* targetFacts = world.FindMember(pTarget->GetObjectGuid());
        if (!myFacts || !targetFacts)
            return;

        float const missing = float(pTarget->GetMaxHealth() - pTarget->GetHealth());

        // Points lost to enemies that turn on me after this heal (0 = safe).
        auto aggroLoss = [&](SpellEntry const* spell, float* effectiveOut)
        {
            float const effective = std::min(missing, EstimateDirectHeal(me, pTarget, spell));
            if (effectiveOut)
                *effectiveOut = effective;
            std::vector<ThreatView::EnemyThreatDelta> deltas;
            ThreatView::PredictHealThreat(me, pTarget, effective, spell, deltas);
            float loss = 0.0f;
            for (ThreatView::EnemyThreatDelta const& d : deltas)
            {
                EnemyFacts const* e = world.FindEnemy(d.enemy);
                if (!e || ThreatView::Headroom(*e, self) - d.added >= 0.0f)
                    continue;
                float const ttd = Value::EnemyTimeToDie(world, *e);
                loss += Value::HitLoss(world, *e, *myFacts, std::min(TANK_RECLAIM_SEC, ttd));
            }
            return loss;
        };

        // Mirror SelectMostEfficientHealingSpell: list is strongest first; pick the closest
        // base value to the missing health, stop at the first one below it.
        std::vector<SpellEntry const*> castable;
        SpellEntry const* predicted = nullptr;
        size_t predictedIndex = 0;
        int32 bestDiff = INT32_MAX;
        bool stopped = false;
        for (SpellEntry const* spell : bot->m_spellListDirectHeal)
        {
            if (!bot->CanTryToCastSpell(pTarget, spell))
                continue;
            castable.push_back(spell);
            if (stopped)
                continue;
            int32 const diff = BasePoints(spell) - int32(missing);
            if (std::abs(diff) < bestDiff)
            {
                bestDiff = diff;
                predicted = spell;
                predictedIndex = castable.size() - 1;
            }
            if (diff < 0)
                stopped = true;
        }

        if (!predicted)
            return;
        float predictedHeal = 0.0f;
        float const loss = aggroLoss(predicted, &predictedHeal);
        if (loss <= 0.0f)
            return;

        // Option 1: a smaller heal that does not pull. Costs the healing it lacks.
        for (size_t i = predictedIndex + 1; i < castable.size(); ++i)
        {
            float smallerHeal = 0.0f;
            if (aggroLoss(castable[i], &smallerHeal) > 0.0f)
                continue;
            Proposal p = MakeProposal(ProposalTier::Assigned, BotActionType::Cast, pTarget->GetObjectGuid(), castable[i]);
            p.reason = "smaller heal, no aggro";
            p.SetValue(loss, Value::Points(world, std::max(0.0f, predictedHeal - smallerHeal)));
            out.push_back(p);
            break;
        }

        // Option 2: do not heal this target this tick; the rest of the rotation runs.
        // Costs the damage it takes until the next tick, and its death if it would not last.
        float const delaySec = 2.0f * sCommanderMgr.GetBotUpdateInterval() / 1000.0f;
        float const incoming = Value::IncomingOn(world, targetFacts->guid);
        float delayCost = Value::Points(world, std::min(missing, incoming * delaySec));
        if (incoming * delaySec >= float(targetFacts->health))
            delayCost += Value::DeathPenalty(*targetFacts);

        Proposal veto;
        veto.kind = ProposalKind::Veto;
        veto.source = "threat";
        veto.reason = "heal would pull aggro";
        veto.veto.type = VetoType::Heal;
        veto.veto.target = pTarget->GetObjectGuid();
        veto.SetValue(loss, delayCost);
        out.push_back(veto);
    }
}
