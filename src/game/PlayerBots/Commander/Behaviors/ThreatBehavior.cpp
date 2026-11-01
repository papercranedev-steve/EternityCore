#include "Commander/Behaviors/ThreatBehavior.h"
#include "Commander/Behaviors/ManaBudgetBehavior.h"
#include "Commander/Perception/ManaBudget.h"
#include "Commander/Perception/ThreatView.h"
#include "Commander/Perception/PackResolver.h"
#include "Commander/Perception/ValueModel.h"
#include "Commander/Perception/ProfileStore.h"
#include "Commander/CommanderMgr.h"
#include "PartyBotAI.h"
#include "Map.h"
#include "Pet.h"
#include "Spell.h"
#include "SpellEntry.h"
#include "SpellMgr.h"
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
        // Pets: see IsHeldByHolder.
        bool IsHolder(PartyMemberFacts const& m)
        {
            if (m.isPet)
                return false;
            return !m.isBot || m.role == ROLE_TANK;
        }

        PartyMemberFacts const* FindTankBot(WorldModel const& world)
        {
            for (PartyMemberFacts const& m : world.party)
                if (m.isBot && !m.isPet && m.alive && m.role == ROLE_TANK)
                    return &m;
            return nullptr;
        }

        // A player (not a bot) who is tanking: set up to tank (PartyMemberFacts::tankStance) and
        // the victim of more than half of the acting enemies fighting us. The player's role is
        // not known; the stance keeps a healer the enemies ran to from counting.
        bool IsTankingPlayer(WorldModel const& world, PartyMemberFacts const& m)
        {
            if (m.isBot || m.isPet || !m.alive || !m.tankStance)
                return false;
            uint32 acting = 0;
            uint32 onHim = 0;
            for (EnemyFacts const& e : world.enemies)
            {
                if (e.onAllyOnly || !e.acting || e.crowdControlled)
                    continue;
                ++acting;
                if (e.victim == m.guid)
                    ++onHim;
            }
            return onHim > 0 && 2 * onHim > acting;
        }

        // Somebody holds the enemies for the party: an alive tank bot, a tank-like ally, or a
        // player who is tanking (IsTankingPlayer).
        bool HasTank(WorldModel const& world)
        {
            if (FindTankBot(world))
                return true;
            for (AllyFacts const& a : world.allies)
                if (a.tankLike)
                    return true;
            for (PartyMemberFacts const& m : world.party)
                if (IsTankingPlayer(world, m))
                    return true;
            return false;
        }

        // A pet spell that makes enemies attack the pet: positive threat (Growl, Torment,
        // Suffering) or a taunt. Cower and Soothing Kiss lower threat (negative value): not taunts.
        bool IsPetTauntSpell(SpellEntry const* spell)
        {
            if (!spell->IsAutocastable())
                return false;
            for (uint32 i = 0; i < MAX_SPELL_EFFECTS; ++i)
            {
                if (spell->Effect[i] == SPELL_EFFECT_ATTACK_ME)
                    return true;
                if (spell->Effect[i] == SPELL_EFFECT_APPLY_AURA && spell->EffectApplyAuraName[i] == SPELL_AURA_MOD_TAUNT)
                    return true;
                if (spell->Effect[i] == SPELL_EFFECT_THREAT && spell->CalculateSimpleValue(SpellEffectIndex(i)) > 0)
                    return true;
            }
            return false;
        }

        // Taunt spells the pet has learned (pets keep only the highest rank).
        void PetTauntSpells(Pet const* pet, std::vector<SpellEntry const*>& out)
        {
            for (auto const& learned : pet->m_petSpells)
            {
                if (learned.second.state == PETSPELL_REMOVED)
                    continue;
                if (SpellEntry const* spell = sSpellMgr.GetSpellEntry(learned.first))
                    if (IsPetTauntSpell(spell))
                        out.push_back(spell);
            }
        }

        // The pet casts one of its taunts by itself (autocast on): it pulls enemies to it.
        bool HasTauntAutocast(Pet const* pet)
        {
            std::vector<SpellEntry const*> taunts;
            PetTauntSpells(pet, taunts);
            for (SpellEntry const* spell : taunts)
                if (std::find(pet->m_autospells.begin(), pet->m_autospells.end(), spell->Id) != pet->m_autospells.end())
                    return true;
            return false;
        }

        // A controlled pet (hunter, warlock) of this party that holds enemies itself: there is
        // no tank and its taunt is on autocast (voidwalker, hunter pet with Growl).
        bool IsHolderPet(PartyBotAI const* bot, WorldModel const& world, ObjectGuid guid)
        {
            if (HasTank(world))
                return false;
            Unit* unit = bot->me->GetMap()->GetUnit(guid);
            Pet const* pet = unit ? unit->ToPet() : nullptr;
            if (!pet || !pet->IsControlled())
                return false;
            return HasTauntAutocast(pet);
        }

        // `victim` holds the enemy it is being hit by: a holder member (above), a holder pet
        // (IsHolderPet), or a tank-like ally (AllyFacts::tankLike, e.g. Sergeant Bly in
        // Zul'Farrak, combat scenario 9).
        // Damage dealers leave room under its threat the same way. Threat rows of allies are
        // on EnemyFacts::threat (partySide = false), so ThreatView::Headroom works unchanged:
        // it compares my threat with victimThreat, whoever the victim is.
        bool IsHeldByHolder(PartyBotAI const* bot, WorldModel const& world, ObjectGuid victim)
        {
            if (PartyMemberFacts const* m = world.FindMember(victim))
                return m->isPet ? IsHolderPet(bot, world, victim) : IsHolder(*m);
            AllyFacts const* ally = world.FindAlly(victim);
            return ally && ally->tankLike;
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

        // Mana the spell costs this caster now (same as ManaBudgetBehavior). 0 = no mana.
        float ManaCost(Player* caster, SpellEntry const* spell)
        {
            if (spell->powerType != POWER_MANA)
                return 0.0f;
            return float(Spell::CalculatePowerCost(spell, caster));
        }

        // Members that keep the higher mana budget heal ceiling (same rule as
        // ManaBudgetBehavior's IsTankLike): tank bots, players or pets being hit right now.
        bool BudgetTankLike(WorldModel const& world, PartyMemberFacts const& m)
        {
            if (m.isBot && !m.isPet)
                return m.role == ROLE_TANK;
            for (EnemyFacts const& e : world.enemies)
                if (e.acting && !e.crowdControlled && e.victim == m.guid)
                    return true;
            return false;
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

        // Warlock imp: casts from range, never melees.
        uint32 const IMP_ENTRY = 416;

        // `who` fights in melee: a pet other than the imp, or a melee damage dealer / tank bot.
        // Players (role unknown) and ranged bots count as ranged.
        bool FightsInMelee(PartyBotAI const* bot, WorldModel const& world, ObjectGuid who)
        {
            PartyMemberFacts const* m = world.FindMember(who);
            if (!m)
                return false;
            if (m->isPet)
            {
                Unit const* unit = bot->me->GetMap()->GetUnit(who);
                return unit && unit->GetEntry() != IMP_ENTRY;
            }
            return m->isBot && (m->role == ROLE_MELEE_DPS || m->role == ROLE_TANK);
        }

        // An enemy `who` (damage dealer or pet at `from`) can hit instead of `current`: held by a
        // holder, enough room in relative terms AND for a few seconds of its threat. The ratio
        // alone picks adds the tank touched once (ratio 1.1, room for one hit) and the damage
        // ends up spread over several half-dead enemies (combat scenario 9). `preferred` first
        // if it is safe, else the safe one closest to dying. Null = none.
        // Room is judged as `who` will fight after switching: a melee unit not in reach of the
        // candidate yet gets the 110% rule (ThreatView::MELEE_AGGRO_FACTOR) instead of the 130%
        // ThreatView::Headroom gives it now, else it switches into the stop band (threat-behavior
        // doc 4.2 / 4.7). Ranged units keep Headroom.
        EnemyFacts const* FindSafeTarget(PartyBotAI const* bot, WorldModel const& world, ObjectGuid who,
                                         Position3 const& from, ObjectGuid current, ObjectGuid preferred)
        {
            float const range = ThreatBehavior::DPS_SWITCH_RANGE;
            float const minRatio = ThreatBehavior::DPS_SWITCH_HEADROOM;
            float const minSec = ThreatBehavior::DPS_SWITCH_MIN_SEC;
            bool const melee = FightsInMelee(bot, world, who);
            EnemyFacts const* best = nullptr;
            for (EnemyFacts const& a : world.enemies)
            {
                if (a.guid == current || a.crowdControlled || !a.acting || a.victim.IsEmpty() || a.victim == who ||
                    a.victimThreat <= 0.0f)
                    continue;
                if (!IsHeldByHolder(bot, world, a.victim))
                    continue;   // switching onto an enemy that is already on a healer does not help
                if (PackResolver::Distance(from, a.pos) > range)
                    continue;
                float room = ThreatView::Headroom(a, who);
                if (melee)
                    if (ThreatEntry const* own = ThreatView::FindEntry(a, who))
                        if (!own->inMeleeReach)
                            room = a.victimThreat * ThreatView::MELEE_AGGRO_FACTOR - own->threat;
                float const ownRate = OwnThreatRate(world, who, a);
                if (room / a.victimThreat < minRatio || room < ownRate * minSec)
                    continue;
                if (!preferred.IsEmpty() && a.guid == preferred)
                    return &a;
                if (!best || a.health < best->health)
                    best = &a;
            }
            return best;
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
        // Pet actions run besides the bot's own action (Arbiter), so both are proposed.
        ProposePet(bot, world, out);

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
            // On an ally (tank-like or not): not loose. The ally is fighting it on purpose and
            // taking it off the ally only moves the damage onto us (combat scenario 9).
            if (world.FindAlly(e.victim))
                continue;
            PartyMemberFacts const* victim = world.FindMember(e.victim);
            if (!victim || victim->charmed)
                continue;   // not one of us, or mind controlled
            // Enemies on pets are loose too: with a tank around, the pet's taunt autocast is off
            // and it moves to a safe enemy (ProposePet), so nobody else takes it back. A few hits
            // on a pet cost little: the value scoring below decides.
            if (victim->isBot && victim->role == ROLE_TANK)
                continue;   // another tank holds it
            // Except a pet that taunts by itself (the player's pet, or a bot pet before its
            // autocast was switched off): it growls the enemy back, and taunting or switching
            // against it is a war the tank loses its main target in.
            if (victim->isPet)
                if (Unit* petUnit = me->GetMap()->GetUnit(victim->guid))
                    if (Pet const* pet = petUnit->ToPet())
                        if (HasTauntAutocast(pet))
                            continue;

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

        // Only protect a holder (also a tank-like ally). If it is on a healer, another dps or an
        // ally that is not tank-like, taking it is not worse.
        if (!IsHeldByHolder(bot, world, e->victim))
            return;

        if (ThreatView::Headroom(*e, self) >= NeededHeadroom(world, self, *e))
            return;
        float const tickSec = sCommanderMgr.GetBotUpdateInterval() / 1000.0f;

        // What pulling it costs: its hits on me until the tank has it back. Weak enemies on
        // plate, or enemies about to die, cost little.
        float const ttd = Value::EnemyTimeToDie(world, *e);
        float const avoided = Value::HitLoss(world, *e, *myFacts, std::min(TANK_RECLAIM_SEC, ttd));

        Position3 myPos;
        myPos.x = me->GetPositionX();
        myPos.y = me->GetPositionY();
        myPos.z = me->GetPositionZ();

        // A safe alternative, the one closest to dying.
        EnemyFacts const* best = FindSafeTarget(bot, world, self, myPos, e->guid, ObjectGuid());
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

    float ThreatBehavior::NeededHeadroom(WorldModel const& world, ObjectGuid who, EnemyFacts const& enemy) const
    {
        // Room left must cover the threat of the next tick, not only 10% of the holder's threat:
        // a fresh add the tank has 0 (or 20) threat on is pulled by the first hit
        // (combat scenario 6, VanCleef's summoned guards). New enemies get a settle window.
        float const tickSec = sCommanderMgr.GetBotUpdateInterval() / 1000.0f;
        float const settleSec = NEW_ENEMY_SETTLE_SEC;
        float const leadSec = NEW_ENEMY_LEAD_SEC;
        bool const settling = enemy.firstSeenMs &&
            WorldTimer::getMSTimeDiff(enemy.firstSeenMs, world.refreshedAtMs) < uint32(settleSec * 1000.0f);
        float const coverSec = settling ? std::max(tickSec, leadSec) : tickSec;
        float const share = DPS_STOP_HEADROOM * enemy.victimThreat;
        float const ownThreat = OwnThreatRate(world, who, enemy) * coverSec;
        return std::max(share, ownThreat);
    }

    // Pet of this bot (hunter, warlock): threat-behavior doc 4.7.
    void ThreatBehavior::ProposePet(PartyBotAI const* bot, WorldModel const& world, std::vector<Proposal>& out) const
    {
        Player* me = bot->me;
        Pet* pet = me->GetPet();
        if (!pet || !pet->IsAlive() || !pet->IsControlled() || pet->IsCharmed() || !pet->GetCharmInfo())
            return;
        ObjectGuid const petGuid = pet->GetObjectGuid();
        PartyMemberFacts const* petFacts = world.FindMember(petGuid);
        if (!petFacts)
            return;

        // Taunt autocast (Growl, Torment, Suffering): off while a tank holds the enemies, on
        // again without one. Proposed only while it differs, so once per change.
        bool const tank = HasTank(world);
        // On again only when nobody is set up to tank: a tanking player is seen only while most
        // enemies hit him, and switching on whenever that dips mid-fight would flip it back and
        // forth. The stance rarely changes in a fight; a damage dealing warrior lets it come back.
        bool maybeTankPlayer = false;
        for (PartyMemberFacts const& m : world.party)
            if (!m.isBot && !m.isPet && m.alive && m.tankStance)
                maybeTankPlayer = true;
        std::vector<SpellEntry const*> taunts;
        PetTauntSpells(pet, taunts);
        for (SpellEntry const* spell : taunts)
        {
            bool const autocast = std::find(pet->m_autospells.begin(), pet->m_autospells.end(), spell->Id) !=
                                  pet->m_autospells.end();
            bool const turnOff = autocast && tank;
            bool const turnOn = !autocast && !tank && !maybeTankPlayer;
            if (!turnOff && !turnOn)
                continue;
            Proposal p = MakeProposal(ProposalTier::Assigned,
                turnOff ? BotActionType::PetAutocastOff : BotActionType::PetAutocastOn, ObjectGuid(), spell);
            p.reason = turnOff ? "tank holds, pet taunt off" : "no tank, pet taunt on";
            out.push_back(p);
        }

        if (world.enemies.empty())
            return;
        // No tank and the pet taunts by itself: it holds enemies (IsHolderPet), leave it.
        if (!tank && HasTauntAutocast(pet))
            return;

        // The enemy the pet hits, else the one the rotation sends it to next (hunter and warlock
        // rotations send an idle pet at the owner's victim).
        Unit const* aim = pet->GetVictim() ? pet->GetVictim() : me->GetVictim();
        if (!aim)
            return;
        EnemyFacts const* e = world.FindEnemy(aim->GetObjectGuid());
        if (!e || e->crowdControlled || !e->acting || e->victim.IsEmpty())
            return;

        PartyMemberFacts const* tankBot = FindTankBot(world);

        // Idle pet in combat (after a PetFollow below, or never sent): nothing in the rotations
        // sends it in again during a fight (only their out of combat parts do, and PetAI only
        // reacts when the pet or the owner is hit). Send it at the owner's target once that has
        // room, with the stricter switch thresholds as hysteresis against the stop rule above,
        // else at another safe enemy. Never PetFollow an idle pet: it is already back.
        if (!pet->GetVictim())
        {
            if (!me->IsInCombat() || e->victim == petGuid)
                return;
            // Busy with its own cast or channel (Consume Shadows when idle): the Attack command
            // starts a chase and breaks it.
            if (pet->IsNonMeleeSpellCasted(false))
                return;
            float const minRatio = DPS_SWITCH_HEADROOM;
            float const minSec = DPS_SWITCH_MIN_SEC;
            // Room as it will fight: a pet called back stands out of melee reach and gets the
            // 130% rule, 0.2 x the holder's threat more than once it is back in (110%), which
            // would send it straight into the stop band. The imp (416) has no melee.
            ThreatEntry const* own = ThreatView::FindEntry(*e, petGuid);
            float const factor = pet->GetEntry() == IMP_ENTRY ? ThreatView::RANGED_AGGRO_FACTOR : ThreatView::MELEE_AGGRO_FACTOR;
            float const room = own ? e->victimThreat * factor - own->threat : ThreatView::NO_LIMIT;
            bool const safe = !IsHeldByHolder(bot, world, e->victim) ||
                (e->victimThreat > 0.0f && room / e->victimThreat >= minRatio &&
                 room >= OwnThreatRate(world, petGuid, *e) * minSec);
            EnemyFacts const* sendAt = safe ? e : FindSafeTarget(bot, world, petGuid, petFacts->pos, e->guid,
                                                                 tankBot ? tankBot->victim : ObjectGuid());
            if (!sendAt)
                return;
            float const tickSec = sCommanderMgr.GetBotUpdateInterval() / 1000.0f;
            Proposal p = MakeProposal(ProposalTier::Assigned, BotActionType::PetAttack, sendAt->guid);
            p.reason = "idle pet, send in";
            // Benefit: the pet's damage over the next ticks; nothing is lost by sending it. At
            // least 1 point: with no measured incoming damage (all enemies on a tank-like ally)
            // LostAction is 0 and the arbiter would drop it.
            p.SetValue(std::max(1.0f, Value::LostAction(world, *petFacts, 2.0f * tickSec)), 0.0f);
            out.push_back(p);
            return;
        }
        bool const holding = e->victim == petGuid;
        if (holding)
        {
            // The pet has it: only a tank bot takes it over (ProposeTank counts enemies on pets
            // as loose). Without one, the pet keeps it.
            if (!tankBot)
                return;
        }
        else if (!IsHeldByHolder(bot, world, e->victim) ||
                 ThreatView::Headroom(*e, petGuid) >= NeededHeadroom(world, petGuid, *e))
            return;

        // Benefit: the hits the pet takes until the tank has the enemy (back).
        float const reclaimSec = TANK_RECLAIM_SEC;
        float const ttd = Value::EnemyTimeToDie(world, *e);
        float const avoided = Value::HitLoss(world, *e, *petFacts, std::min(reclaimSec, ttd));

        // A safe enemy, the tank's target first: the pet then fights next to the tank, and an
        // enemy hitting the pet follows it there.
        EnemyFacts const* best = FindSafeTarget(bot, world, petGuid, petFacts->pos, e->guid,
                                                tankBot ? tankBot->victim : ObjectGuid());
        if (best)
        {
            Proposal p = MakeProposal(ProposalTier::Assigned, BotActionType::PetAttack, best->guid);
            p.reason = holding ? "pet holds tank's enemy, switch" : "pet near aggro, switch";
            p.SetValue(avoided, Value::LostAction(world, *petFacts, SWITCH_TARGET_SEC));
            out.push_back(p);
        }

        // Calling back a pet that is being hit brings the enemy to the owner: switch only.
        if (holding)
            return;
        float const tickSec = sCommanderMgr.GetBotUpdateInterval() / 1000.0f;
        Proposal p = MakeProposal(ProposalTier::Assigned, BotActionType::PetFollow);
        p.reason = "pet near aggro, come back";
        p.SetValue(avoided, Value::LostAction(world, *petFacts, tickSec));
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
            // Health missing when the heal lands, the same estimate CastWatch checks mid-cast.
            // With the missing health of now, the arbiter approved heals that CastWatch cancelled
            // 0.3 s later and the rotation started again (9 of 24 Heals in the second Stockade
            // test, 2026-11-01).
            float const castSec = float(spell->GetCastTime(me)) / 1000.0f;
            float const atLanding = std::min(float(target->GetMaxHealth()),
                                             targetMissing + Value::IncomingOn(world, target->GetObjectGuid()) * castSec);
            float const effective = std::min(atLanding, EstimateDirectHeal(me, target, spell));
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
        // Mana budget (ManaBudgetBehavior::ComputeBudget, same numbers that behavior uses): when
        // mana is short (tension > 1), only members below their heal ceiling, and no heal that
        // ManaBudgetBehavior would swap for a cheaper, more efficient rank.
        ManaBudgetBehavior::Budget budget;
        // Only while the mana module runs: with it off, nobody else limits heals either.
        bool const budgeted = sCommanderMgr.IsModuleEnabled("mana") &&
            ManaBudgetBehavior::ComputeBudget(bot, world, budget) && budget.tension > 1.0f;

        Unit* other = nullptr;
        PartyMemberFacts const* otherFacts = nullptr;
        for (PartyMemberFacts const& m : world.party)
        {
            if (m.guid == targetFacts->guid || m.isPet || !m.alive || m.charmed || m.healthPct >= OTHER_HEAL_PCT)
                continue;
            if (budgeted && m.healthPct >= ManaBudget::HealCeilingPct(budget.tension, BudgetTankLike(world, m)))
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
            // ManaBudgetBehavior's swap rule: a smaller rank that costs less, heals at least
            // SWAP_MIN_COVER of it and heals more per mana makes this one too expensive.
            float const swapCover = ManaBudgetBehavior::SWAP_MIN_COVER;
            auto overBudget = [&](size_t index)
            {
                if (!budgeted)
                    return false;
                float const bigCost = ManaCost(me, otherCastable[index]);
                float const bigHeal = std::min(otherMissing, EstimateDirectHeal(me, other, otherCastable[index]));
                if (bigCost <= 0.0f || bigHeal <= 0.0f)
                    return false;
                for (size_t j = index + 1; j < otherCastable.size(); ++j)
                {
                    float const smallCost = ManaCost(me, otherCastable[j]);
                    if (smallCost <= 0.0f || smallCost >= bigCost)
                        continue;
                    float const smallHeal = std::min(otherMissing, EstimateDirectHeal(me, other, otherCastable[j]));
                    if (smallHeal >= swapCover * bigHeal && smallHeal / smallCost > bigHeal / bigCost)
                        return true;
                }
                return false;
            };
            if (predictHeal(other, otherMissing, otherCastable, otherIndex))
            {
                // The rotation's choice for that member first, then smaller ones, until one is
                // safe and within the mana budget. CanTryToCastSpell (predictHeal) already left
                // out heals the bot cannot pay for.
                for (size_t i = otherIndex; i < otherCastable.size(); ++i)
                {
                    SpellEntry const* spell = otherCastable[i];
                    if (overBudget(i))
                        continue;
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
