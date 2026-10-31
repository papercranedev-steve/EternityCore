#include "Commander/Behaviors/ThreatBehavior.h"
#include "Commander/Perception/ThreatView.h"
#include "Commander/Perception/PackResolver.h"
#include "PartyBotAI.h"
#include "SpellEntry.h"
#include "Timer.h"
#include <algorithm>
#include <cmath>

namespace Commander
{
    namespace
    {
        Proposal MakeProposal(ProposalTier tier, float score, BotActionType type, ObjectGuid target = ObjectGuid(),
                              SpellEntry const* spell = nullptr)
        {
            Proposal p;
            p.tier = tier;
            p.score = score;
            p.source = "threat";
            p.action.type = type;
            p.action.target = target;
            p.action.spell = spell;
            return p;
        }

        bool IsClothClass(uint8 classId)
        {
            return classId == CLASS_MAGE || classId == CLASS_PRIEST || classId == CLASS_WARLOCK;
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

    float ThreatBehavior::Tolerance(PartyMemberFacts const& victim, EnemyFacts const& enemy)
    {
        bool const fragile = (victim.isBot && victim.role == ROLE_HEALER) || IsClothClass(victim.classId);
        float tolerance = fragile ? TOLERANCE_FRAGILE_SEC : TOLERANCE_STURDY_SEC;
        tolerance *= std::max(TOLERANCE_MIN_HEALTH_FACTOR, victim.healthPct / 100.0f);
        if (enemy.elite)
            tolerance *= TOLERANCE_ELITE_FACTOR;
        return tolerance;
    }

    // Tank: threat-behavior doc section 3.
    void ThreatBehavior::ProposeTank(PartyBotAI const* bot, WorldModel const& world, std::vector<Proposal>& out) const
    {
        Player* me = bot->me;
        ObjectGuid const self = me->GetObjectGuid();
        Unit const* pVictim = me->GetVictim();
        ObjectGuid const myTarget = pVictim ? pVictim->GetObjectGuid() : ObjectGuid();
        uint32 const now = WorldTimer::getMSTime();

        // What we can generate: best observed threat rate on any enemy.
        float tankRate = 0.0f;
        for (EnemyFacts const& e : world.enemies)
            if (ThreatEntry const* t = ThreatView::FindEntry(e, self))
                tankRate = std::max(tankRate, t->threatPerSec);

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

            float const gap = std::max(0.0f, ThreatView::Headroom(e, self));
            float const recovery = tankRate > 1.0f ? gap / tankRate : ThreatView::NO_LIMIT;
            float const tolerance = Tolerance(*victim, e);
            float const looseFor = WorldTimer::getMSTimeDiff(e.victimSinceMs, now) / 1000.0f;
            float const danger = 1.0f / std::max(0.1f, tolerance);

            if (looseFor >= tolerance || recovery > tolerance - looseFor)
            {
                for (SpellEntry const* pTaunt : bot->m_spellListTaunt)
                    out.push_back(MakeProposal(ProposalTier::Urgent, 10.0f + danger, BotActionType::Cast, e.guid, pTaunt));
            }

            // Win it back with threat (also the fallback when the taunt is on cooldown).
            // If it already is our target, the rotation builds threat on it.
            if (e.guid != myTarget)
                out.push_back(MakeProposal(ProposalTier::Urgent, danger, BotActionType::Attack, e.guid));
        }

        // 2. Top up the enemy we hold with the smallest lead.
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
            out.push_back(MakeProposal(ProposalTier::Assigned, 1.0f - weakestRatio, BotActionType::Attack, weakest->guid));
    }

    // Damage dealers: stop before pulling aggro off a holder.
    void ThreatBehavior::ProposeDamage(PartyBotAI const* bot, WorldModel const& world, std::vector<Proposal>& out) const
    {
        Player* me = bot->me;
        Unit const* pVictim = me->GetVictim();
        if (!pVictim)
            return;

        ObjectGuid const self = me->GetObjectGuid();
        EnemyFacts const* e = world.FindEnemy(pVictim->GetObjectGuid());
        if (!e || e->victim.IsEmpty() || e->victim == self)
            return;

        // Only protect a holder. If it is on a healer or another dps, taking it is not worse.
        PartyMemberFacts const* holder = world.FindMember(e->victim);
        if (!holder || !IsHolder(*holder))
            return;

        if (ThreatView::Headroom(*e, self) >= DPS_STOP_HEADROOM * e->victimThreat)
            return;

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
            out.push_back(MakeProposal(ProposalTier::Assigned, 0.5f, BotActionType::Attack, best->guid));
        out.push_back(MakeProposal(ProposalTier::Assigned, 0.4f, BotActionType::HoldFire));
    }

    // Healers: do not pull aggro with a heal unless the target is in danger.
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

        float const missing = float(pTarget->GetMaxHealth() - pTarget->GetHealth());
        ObjectGuid const self = me->GetObjectGuid();

        auto isSafe = [&](SpellEntry const* spell)
        {
            float const effective = std::min(missing, EstimateDirectHeal(me, pTarget, spell));
            std::vector<ThreatView::EnemyThreatDelta> deltas;
            ThreatView::PredictHealThreat(me, pTarget, effective, spell, deltas);
            return ThreatView::WorstHeadroomAfter(world, self, deltas) >= 0.0f;
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

        if (!predicted || isSafe(predicted))
            return;

        for (size_t i = predictedIndex + 1; i < castable.size(); ++i)
        {
            if (isSafe(castable[i]))
            {
                out.push_back(MakeProposal(ProposalTier::Assigned, 0.6f, BotActionType::Cast, pTarget->GetObjectGuid(), castable[i]));
                return;
            }
        }

        // Nothing safe: wait for the tank to build threat.
        out.push_back(MakeProposal(ProposalTier::Assigned, 0.3f, BotActionType::Hold));
    }
}
