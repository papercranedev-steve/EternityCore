#include "Commander/Behaviors/ThreatBehavior.h"
#include "Commander/Perception/ThreatView.h"
#include "Commander/Perception/PackResolver.h"
#include "Commander/Perception/ValueModel.h"
#include "Commander/Perception/ProfileStore.h"
#include "Commander/CommanderMgr.h"
#include "PartyBotAI.h"
#include "Map.h"
#include "SpellEntry.h"
#include "Timer.h"
#include <algorithm>
#include <cmath>
#include <utility>

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

        // Threat `self` adds per second to `enemy`: measured on it, else the best rate measured on
        // any enemy (just switched), else a level-based guess (fight just started).
        float OwnThreatRate(WorldModel const& world, ObjectGuid self, EnemyFacts const& enemy)
        {
            if (ThreatEntry const* t = ThreatView::FindEntry(enemy, self))
                if (t->threatPerSec > 0.0f)
                    return t->threatPerSec;
            float best = 0.0f;
            for (EnemyFacts const& e : world.enemies)
                if (ThreatEntry const* t = ThreatView::FindEntry(e, self))
                    best = std::max(best, t->threatPerSec);
            if (best > 0.0f)
                return best;
            PartyMemberFacts const* me = world.FindMember(self);
            return me ? Value::FALLBACK_DPS_PER_LEVEL * float(me->level) : 0.0f;
        }

        // Area threat abilities of a tank. `share` = threat put on each enemy, as a fraction of
        // what one global cooldown of single target threat gives (rough, per ability).
        struct GroupThreatOption
        {
            SpellEntry const* spell;
            float share;
            bool taunt;         // forces every enemy around onto the tank (Challenging Roar)
            bool onTarget;      // cast at the current victim (Thunder Clap, Swipe) instead of self
        };

        std::vector<GroupThreatOption> GroupThreatOptions(PartyBotAI const* bot)
        {
            auto const& s = bot->m_spells;
            switch (bot->me->GetClass())
            {
                case CLASS_WARRIOR:
                    return { { s.warrior.pThunderClap, 1.0f, false, true },
                             { s.warrior.pDemoralizingShout, 0.3f, false, false } };
                case CLASS_PALADIN:
                    return { { s.paladin.pConsecration, 0.5f, false, false } };
                case CLASS_DRUID:
                    return { { s.druid.pSwipe, 0.8f, false, true },
                             { s.druid.pDemoralizingRoar, 0.3f, false, false },
                             { s.druid.pChallengingRoar, 0.0f, true, false } };
                default:
                    return {};
            }
        }

        // One proposal per usable area threat ability. `valueOf(threatAdded, tauntSec)` returns
        // the benefit over every loose enemy around the tank and whether someone would die.
        template <typename ValueFn>
        void ProposeGroupThreat(PartyBotAI const* bot, WorldModel const& world, float tankRate,
                                float switchCost, float enemyMelee, ValueFn valueOf, std::vector<Proposal>& out)
        {
            Unit* victim = bot->me->GetVictim();
            for (GroupThreatOption const& o : GroupThreatOptions(bot))
            {
                if (!o.spell)
                    continue;
                Unit* castTarget = o.onTarget ? victim : bot->me;
                if (!castTarget || !bot->CanTryToCastSpell(castTarget, o.spell))
                    continue;

                float const threat = tankRate * Value::GCD_SEC * o.share;
                std::pair<float, bool> const value = valueOf(threat, o.taunt ? 1.0f : 0.0f);
                float cost = switchCost;
                if (o.taunt)    // long cooldown: kept for a real emergency
                    cost += Value::Points(world, enemyMelee * ThreatBehavior::TAUNT_RESERVE_SEC) *
                            ThreatBehavior::GROUP_TAUNT_RESERVE_MULT;

                Proposal p = MakeProposal(value.second ? ProposalTier::Urgent : ProposalTier::Assigned,
                                          BotActionType::Cast, o.onTarget ? victim->GetObjectGuid() : ObjectGuid(), o.spell);
                p.reason = o.taunt ? "group taunt loose enemies" : "area threat on loose enemies";
                p.SetValue(value.first, cost);
                out.push_back(p);
            }
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
            if (!e.crowdControlled && e.acting)
                enemyMelee += e.meleeDps + e.rangedDps;
        float const switchCost = Value::LostAction(world, *myFacts, Value::GCD_SEC);

        // Enemies hitting someone else that the tank should consider for the group threat
        // proposal below: (enemy, gap, loss doing nothing, victim).
        struct LooseEnemy
        {
            EnemyFacts const* enemy;
            PartyMemberFacts const* victim;
            float gap;
            float horizon;
            float lossNothing;
            float mult;
        };
        std::vector<LooseEnemy> loose;

        // 1. Loose enemies hitting someone who is not a tank.
        for (EnemyFacts const& e : world.enemies)
        {
            if (e.crowdControlled || !e.acting || e.victim.IsEmpty() || e.victim == self)
                continue;
            PartyMemberFacts const* victim = world.FindMember(e.victim);
            if (!victim || victim->isPet || victim->charmed)
                continue;   // not one of us, a pet (pets can take a few hits), or mind controlled
            if (victim->isBot && victim->role == ROLE_TANK)
                continue;   // another tank holds it

            // Only this fight: an enemy in another room hitting a teleported member (Gandling's
            // Shadow Portal) or far away is not ours to run after (combat scenario 10).
            Unit const* unit = me->GetMap()->GetUnit(e.guid);
            if (!unit || !me->IsWithinDistInMap(unit, TANK_REACH_RANGE) || !me->IsWithinLOSInMap(unit))
                continue;
            float const distance = me->GetDistance(unit);
            // Walking over: lost time, and the enemies we hold follow us away from the healer.
            float const walkCost = Value::LostAction(world, *myFacts,
                std::max(0.0f, distance - MELEE_REACH) / RUN_YARDS_PER_SEC);

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
                p.SetValue((lossNothing - lossThreat) * mult, switchCost + walkCost);
                out.push_back(p);
            }

            if (distance <= GROUP_THREAT_RADIUS)
                loose.push_back({ &e, victim, gap, horizon, lossNothing, mult });
        }

        // Enemies on the tank with a lead thinner than one global cooldown of tank threat:
        // fresh adds at 0 threat that the next heal pulls (VanCleef's guards, scenario 6).
        struct FragileEnemy
        {
            EnemyFacts const* enemy;
            PartyMemberFacts const* runnerUp;
        };
        std::vector<FragileEnemy> fragile;
        float const thinLead = tankRate * Value::GCD_SEC;
        for (EnemyFacts const& e : world.enemies)
        {
            if (e.crowdControlled || !e.acting || e.victim != self)
                continue;
            if (PackResolver::Distance(myFacts->pos, e.pos) > GROUP_THREAT_RADIUS)
                continue;
            ObjectGuid closest;
            if (ThreatView::VictimLead(e, &closest) >= thinLead)
                continue;
            if (PartyMemberFacts const* runnerUp = world.FindMember(closest))
                if (!runnerUp->isPet)
                    fragile.push_back({ &e, runnerUp });
        }

        // 1b. Several loose or fragile enemies around the tank (summoned adds, a wave arriving):
        // one area threat ability (Thunder Clap, Consecration, Challenging Roar) instead of one
        // switch per enemy per tick (combat scenarios 6 and 9).
        if (loose.size() + fragile.size() >= GROUP_THREAT_MIN_ENEMIES)
            ProposeGroupThreat(bot, world, tankRate, switchCost, enemyMelee,
                [&](float threatAdded, float tauntSec) -> std::pair<float, bool>
                {
                    float benefit = 0.0f;
                    bool deadly = false;
                    // A fragile enemy would turn on the runner-up for a moment; the area ability
                    // makes that less likely (counted at FRAGILE_WEIGHT of that loss).
                    if (threatAdded > 0.0f || tauntSec > 0.0f)
                        for (FragileEnemy const& f : fragile)
                            benefit += Value::HitLoss(world, *f.enemy, *f.runnerUp, TANK_RECLAIM_SEC) * FRAGILE_WEIGHT;
                    for (LooseEnemy const& l : loose)
                    {
                        float const rest = std::max(0.0f, l.gap - threatAdded);
                        float recovery = tankRate > 1.0f ? rest / tankRate : ThreatView::NO_LIMIT;
                        if (tauntSec > 0.0f)
                            recovery = std::min(recovery, TAUNT_REACTION_SEC);
                        float const loss = Value::HitLoss(world, *l.enemy, *l.victim, std::min(recovery, l.horizon));
                        benefit += (l.lossNothing - loss) * l.mult;
                        deadly = deadly || Value::SurviveSec(*l.enemy, *l.victim) < l.horizon;
                    }
                    return { benefit, deadly };
                }, out);

        // 2. Top up the enemy we hold with the smallest lead (not valued yet).
        EnemyFacts const* weakest = nullptr;
        float weakestRatio = ThreatView::NO_LIMIT;
        float currentRatio = ThreatView::NO_LIMIT;
        for (EnemyFacts const& e : world.enemies)
        {
            if (e.crowdControlled || !e.acting || e.victim != self)
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

        // Room left must cover the threat of my next tick, not only 10% of the holder's threat:
        // a fresh add the tank has 0 (or 20) threat on is pulled by the first hit
        // (combat scenario 6, VanCleef's summoned guards). New enemies get a settle window.
        float const tickSec = sCommanderMgr.GetBotUpdateInterval() / 1000.0f;
        bool const settling = e->firstSeenMs &&
            WorldTimer::getMSTimeDiff(e->firstSeenMs, world.refreshedAtMs) < uint32(NEW_ENEMY_SETTLE_SEC * 1000.0f);
        float const coverSec = settling ? std::max(tickSec, NEW_ENEMY_LEAD_SEC) : tickSec;
        float const needed = std::max(DPS_STOP_HEADROOM * e->victimThreat, OwnThreatRate(world, self, *e) * coverSec);
        if (ThreatView::Headroom(*e, self) >= needed)
            return;

        // What pulling it costs: its hits on me until the tank has it back. Weak enemies on
        // plate, or enemies about to die, cost little.
        float const ttd = Value::EnemyTimeToDie(world, *e);
        float const avoided = Value::HitLoss(world, *e, *myFacts, std::min(TANK_RECLAIM_SEC, ttd));

        Position3 myPos;
        myPos.x = me->GetPositionX();
        myPos.y = me->GetPositionY();
        myPos.z = me->GetPositionZ();

        // A safe alternative: enough room in relative terms AND for a few seconds of my damage.
        // The ratio alone picks adds the tank touched once (ratio 1.1, room for one hit) and the
        // damage ends up spread over several half-dead enemies (combat scenario 9). Among the
        // safe ones, the one closest to dying.
        EnemyFacts const* best = nullptr;
        for (EnemyFacts const& a : world.enemies)
        {
            if (a.guid == e->guid || a.crowdControlled || !a.acting || a.victim.IsEmpty() || a.victim == self ||
                a.victimThreat <= 0.0f)
                continue;
            PartyMemberFacts const* aHolder = world.FindMember(a.victim);
            if (!aHolder || !IsHolder(*aHolder))
                continue;   // switching onto an enemy that is already on a healer does not help
            if (PackResolver::Distance(myPos, a.pos) > DPS_SWITCH_RANGE)
                continue;
            float const room = ThreatView::Headroom(a, self);
            float const ownRate = OwnThreatRate(world, self, a);
            if (room / a.victimThreat < DPS_SWITCH_HEADROOM || room < ownRate * DPS_SWITCH_MIN_SEC)
                continue;
            if (!best || a.health < best->health)
                best = &a;
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

        // An enemy we have no facts on yet: assume it hits like the average acting enemy.
        float unknownEnemyLoss = 0.0f;
        {
            uint32 count = 0;
            for (EnemyFacts const& e : world.enemies)
            {
                if (!e.acting)
                    continue;
                unknownEnemyLoss += Value::HitLoss(world, e, *myFacts, TANK_RECLAIM_SEC);
                ++count;
            }
            unknownEnemyLoss = count ? unknownEnemyLoss / float(count) : 0.0f;
        }

        // Points lost to enemies that turn on me after this heal on `target` (0 = safe).
        auto aggroLoss = [&](Unit* target, float targetMissing, SpellEntry const* spell, float* effectiveOut)
        {
            float const effective = std::min(targetMissing, EstimateDirectHeal(me, target, spell));
            if (effectiveOut)
                *effectiveOut = effective;
            std::vector<ThreatView::EnemyThreatDelta> deltas;
            ThreatView::PredictHealThreat(me, target, effective, spell, deltas);
            float loss = 0.0f;
            for (ThreatView::EnemyThreatDelta const& d : deltas)
            {
                if (d.added <= 0.0f)
                    continue;
                EnemyFacts const* e = world.FindEnemy(d.enemy);
                if (!e)
                {
                    // On the live threat refs but not in the snapshot yet: an add summoned since
                    // the last refresh, with 0 threat on everyone. Any heal pulls it (scenario 6).
                    loss += unknownEnemyLoss;
                    continue;
                }
                if (ThreatView::Headroom(*e, self) - d.added >= 0.0f)
                    continue;
                float const ttd = Value::EnemyTimeToDie(world, *e);
                loss += Value::HitLoss(world, *e, *myFacts, std::min(TANK_RECLAIM_SEC, ttd));
            }
            return loss;
        };

        // Mirror SelectMostEfficientHealingSpell: list is strongest first; pick the closest
        // base value to the missing health, stop at the first one below it.
        // `castable` = every direct heal usable on `target`, strongest first.
        auto predictHeal = [&](Unit* target, float targetMissing, std::vector<SpellEntry const*>& castable,
                               size_t& predictedIndex) -> SpellEntry const*
        {
            SpellEntry const* predicted = nullptr;
            int32 bestDiff = INT32_MAX;
            bool stopped = false;
            for (SpellEntry const* spell : bot->m_spellListDirectHeal)
            {
                if (!bot->CanTryToCastSpell(target, spell))
                    continue;
                castable.push_back(spell);
                if (stopped)
                    continue;
                int32 const diff = BasePoints(spell) - int32(targetMissing);
                if (std::abs(diff) < bestDiff)
                {
                    bestDiff = diff;
                    predicted = spell;
                    predictedIndex = castable.size() - 1;
                }
                if (diff < 0)
                    stopped = true;
            }
            return predicted;
        };

        std::vector<SpellEntry const*> castable;
        size_t predictedIndex = 0;
        SpellEntry const* predicted = predictHeal(pTarget, missing, castable, predictedIndex);
        if (!predicted)
            return;
        float predictedHeal = 0.0f;
        float const loss = aggroLoss(pTarget, missing, predicted, &predictedHeal);
        if (loss <= 0.0f)
            return;

        // Option 1: a smaller heal that does not pull. Costs the healing it lacks.
        for (size_t i = predictedIndex + 1; i < castable.size(); ++i)
        {
            float smallerHeal = 0.0f;
            if (aggroLoss(pTarget, missing, castable[i], &smallerHeal) > 0.0f)
                continue;
            Proposal p = MakeProposal(ProposalTier::Assigned, BotActionType::Cast, pTarget->GetObjectGuid(), castable[i]);
            p.reason = "smaller heal, no aggro";
            p.SetValue(loss, Value::Points(world, std::max(0.0f, predictedHeal - smallerHeal)));
            out.push_back(p);
            break;
        }

        float const delaySec = 2.0f * sCommanderMgr.GetBotUpdateInterval() / 1000.0f;

        // Option 2: somebody else needs healing too. A veto on this target stops
        // FindAndHealInjuredAlly for the whole tick (SelectHealTarget keeps choosing this target),
        // so nobody would be healed; ManaBudgetBehavior's othersNeedIt check avoids the same trap.
        // Instead heal the most injured other member directly, with a heal that does not pull.
        // Trade-off: no veto is set then. If this cast fails at execution, the rotation runs
        // unvetoed and may cast the pulling heal on this target (unless an earlier veto is still
        // active). Accepted: CanTryToCastSpell already passed, failures are rare, and the
        // alternative (veto + cast) brings back "nobody healed" whenever the cast fails.
        // Same eligibility as SelectHealTarget (IsValidHealTarget: helpful, line of sight, 30 yd).
        // Pets are left out (the rotation only heals them when no player is hurt).
        Unit* other = nullptr;
        PartyMemberFacts const* otherFacts = nullptr;
        for (PartyMemberFacts const& m : world.party)
        {
            if (m.guid == targetFacts->guid || m.isPet || !m.alive || m.charmed || m.healthPct >= OTHER_HEAL_PCT)
                continue;
            if (otherFacts && m.healthPct >= otherFacts->healthPct)
                continue;
            Unit* unit = me->GetMap()->GetUnit(m.guid);
            if (!unit || !bot->IsValidHealTarget(unit, OTHER_HEAL_PCT))
                continue;
            other = unit;
            otherFacts = &m;
        }
        if (other)
        {
            float const otherMissing = float(other->GetMaxHealth() - other->GetHealth());
            float const otherIncoming = Value::IncomingOn(world, otherFacts->guid);
            std::vector<SpellEntry const*> otherCastable;
            size_t otherIndex = 0;
            if (predictHeal(other, otherMissing, otherCastable, otherIndex))
            {
                // The rotation's choice for that member first, then smaller ones, until one is safe.
                for (size_t i = otherIndex; i < otherCastable.size(); ++i)
                {
                    SpellEntry const* spell = otherCastable[i];
                    float healed = 0.0f;
                    if (aggroLoss(other, otherMissing, spell, &healed) > 0.0f)
                        continue;
                    // Benefit: the health it restores, plus the death it prevents when the
                    // member would not last until the next chance to heal it.
                    float benefit = Value::Points(world, healed);
                    if (otherIncoming * delaySec >= float(otherFacts->health))
                        benefit += Value::DeathPenalty(*otherFacts);
                    // Cost: the time spent casting (a global cooldown for instant heals).
                    float const castSec = std::max(Value::GCD_SEC, float(spell->GetCastTime(me)) / 1000.0f);
                    float const cost = Value::LostAction(world, *myFacts, castSec);
                    if (benefit <= cost)
                        continue;   // the arbiter would drop it; a smaller, faster heal may still pay
                    Proposal p = MakeProposal(ProposalTier::Assigned, BotActionType::Cast, other->GetObjectGuid(), spell);
                    p.reason = "heal another member, no aggro";
                    p.SetValue(benefit, cost);
                    out.push_back(p);
                    return;     // no veto, see the trade-off above
                }
            }
        }
        // No other member to heal, or no safe heal worth it on them: veto as before.

        // Option 3: do not heal this target this tick. The rotation still runs (dispels, damage),
        // but its heal goes to this target, so nobody is healed this tick (see option 2).
        // Costs the damage it takes until the next tick, and its death if it would not last.
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
