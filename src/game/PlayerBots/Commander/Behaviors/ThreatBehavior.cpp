#include "Commander/Behaviors/ThreatBehavior.h"
#include "Commander/Behaviors/ManaBudgetBehavior.h"
#include "Commander/Behaviors/HealPlanBehavior.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/ManaBudget.h"
#include "Commander/Perception/ThreatView.h"
#include "Commander/Perception/PackResolver.h"
#include "Commander/Perception/ValueModel.h"
#include "Commander/Perception/ProfileStore.h"
#include "Commander/Perception/Tanks.h"
#include "Commander/Perception/LooseEnemy.h"
#include "Commander/Perception/ThreatRoom.h"
#include "Commander/CommanderMgr.h"
#include "Commander/Classes/ClassKit.h"
#include "PartyBotAI.h"
#include "HealSpells.h"
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

        // Somebody whose job is holding enemies: a tank (Tanks.h), or the player (whatever his
        // role, damage dealers do not pull an enemy off him onto themselves). Pets: see IsHeldByHolder.
        bool IsHolder(PartyMemberFacts const& m)
        {
            return Tanks::IsHolder(m);
        }

        // Somebody holds the enemies for the party: an alive tank (bot or player) or a tank-like
        // ally. Before 2026-11-01 a player counted only in a tank stance while holding more than
        // half of the enemies; the role (Tanks.h) replaces that guess.
        bool HasTank(WorldModel const& world)
        {
            return Tanks::Any(world);
        }

        // Another alive tank bot that can act is nearer to `pos` than `distance` - TANK_SHARE_MARGIN.
        bool CloserTankBot(WorldModel const& world, ObjectGuid self, Position3 const& pos, float distance)
        {
            return Tanks::CloserBot(world, self, pos, distance, ThreatBehavior::TANK_SHARE_MARGIN);
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

        // Members that keep the higher mana budget heal ceiling (same rule as
        // ManaBudgetBehavior's IsTankLike): tank bots, players or pets being hit right now.
        bool BudgetTankLike(WorldModel const& world, PartyMemberFacts const& m)
        {
            if (Tanks::IsTank(m))
                return true;
            if (m.isBot && !m.isPet)
                return false;
            for (EnemyFacts const& e : world.enemies)
                if (e.acting && !e.crowdControlled && e.victim == m.guid)
                    return true;
            return false;
        }

        // Threat `self` adds per second to `enemy`: measured on it, else the best rate measured on
        // any enemy (just switched), else a level-based guess (fight just started).
        float OwnThreatRate(WorldModel const& world, ObjectGuid self, EnemyFacts const& enemy)
        {
            return ThreatRoom::OwnRate(world, self, enemy);
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

        // Area threat abilities of a tank: ClassKit::AreaThreat (Classes/<Class>.cpp).
        using GroupThreatOption = AreaThreatSpell;

        // Seconds a taunt holds an enemy only through its aura (Mocking Blow, Challenging Shout,
        // Challenging Roar): without SPELL_EFFECT_ATTACK_ME the threat it lends is taken back
        // when the aura fades (ThreatManager::tauntFadeOut; strat-20261101-200215: Challenging
        // Shout +748, -714 six seconds later). 0 = the threat stays (Taunt, Growl).
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

        // Seconds the enemy still hits its victim after a taunt: until the taunt lands; for a
        // temporary taunt also after it fades, until the threat built meanwhile closes the gap.
        float TauntSecOnVictim(float gap, float tankRate, float holdSec, float horizon)
        {
            float sec = std::min(ThreatBehavior::TAUNT_REACTION_SEC, horizon);
            if (holdSec > 0.0f && holdSec < horizon)
            {
                float const rest = std::max(0.0f, gap - tankRate * holdSec);
                float const recovery = tankRate > 1.0f ? rest / tankRate : ThreatView::NO_LIMIT;
                sec += std::min(recovery, horizon - holdSec);
            }
            return sec;
        }

        // Threat one cast puts on each enemy it hits: flat spell_threat + direct damage x its
        // multiplier (Spell::HandleThreatSpells, Unit::DealDamage), with the caster's threat
        // modifiers and a warrior's stance swap (PartyBotAI::StanceSwapThreatFactor).
        // Demoralizing Shout 43 flat; Thunder Clap 103 damage x 2.5. Spells without data
        // (Consecration ticks) fall back to `share` of a global cooldown of the tank's rate.
        float ThreatPerEnemy(PartyBotAI const* bot, GroupThreatOption const& o, float tankRate)
        {
            SpellEntry const* spell = o.spell;
            SpellThreatEntry const* entry = sSpellMgr.GetSpellThreatEntry(spell->Id);
            float damage = 0.0f;
            for (uint32 i = 0; i < MAX_SPELL_EFFECTS; ++i)
                if (spell->Effect[i] == SPELL_EFFECT_SCHOOL_DAMAGE)
                    damage += float(std::max(0, spell->CalculateSimpleValue(SpellEffectIndex(i))));
            if (!entry && damage <= 0.0f)
                return tankRate * Value::GCD_SEC * o.share;
            float threat = (entry ? float(entry->threat) : 0.0f) + damage * sSpellMgr.GetSpellThreatMultiplier(spell);
            threat = bot->me->ApplyTotalThreatModifier(threat, spell->GetSpellSchoolMask());
            return threat * bot->StanceSwapThreatFactor(spell);
        }

        bool DealsDamage(SpellEntry const* spell)
        {
            for (uint32 i = 0; i < MAX_SPELL_EFFECTS; ++i)
                if (spell->Effect[i] == SPELL_EFFECT_SCHOOL_DAMAGE || spell->Effect[i] == SPELL_EFFECT_WEAPON_DAMAGE ||
                    spell->Effect[i] == SPELL_EFFECT_WEAPON_DAMAGE_NOSCHOOL || spell->Effect[i] == SPELL_EFFECT_NORMALIZED_WEAPON_DMG ||
                    spell->Effect[i] == SPELL_EFFECT_WEAPON_PERCENT_DAMAGE ||
                    (spell->Effect[i] == SPELL_EFFECT_APPLY_AURA && spell->EffectApplyAuraName[i] == SPELL_AURA_PERIODIC_DAMAGE) ||
                    spell->Effect[i] == SPELL_EFFECT_PERSISTENT_AREA_AURA)
                    return true;
            return false;
        }

        // One proposal per usable area threat ability. `valueOf(threatAdded, holdSec)` returns
        // the benefit over every loose enemy around the tank and whether someone would die
        // (holdSec > 0: an area taunt holding for that long, TemporaryTauntSec).
        template <typename ValueFn>
        void ProposeGroupThreat(PartyBotAI const* bot, WorldModel const& world, TeamPlan const& plan, float tankRate,
                                float switchCost, float stanceCost, float enemyMelee, ValueFn valueOf,
                                std::vector<Proposal>& out)
        {
            Unit* victim = bot->me->GetVictim();
            // Damage breaks crowd control (Polymorph, Sap, Shackle) on enemies around the tank:
            // the plan's constraint (TeamPlan::KeepsUnbroken: breakable controls on them, or
            // about to be put on them). A stun or fear that damage does not end is no reason.
            bool crowdControlNear = false;
            Position3 myPos;
            myPos.x = bot->me->GetPositionX();
            myPos.y = bot->me->GetPositionY();
            myPos.z = bot->me->GetPositionZ();
            for (EnemyFacts const& e : world.enemies)
                crowdControlNear = crowdControlNear ||
                    (plan.KeepsUnbroken(e) && PackResolver::Distance(myPos, e.pos) <= ThreatBehavior::GROUP_THREAT_RADIUS);
            // A warrior's Thunder Clap from Defensive Stance is cast after a stance swap
            // (Arbiter -> PartyBotAI::DoCastWithStance), costed with STANCE_SWAP_COST_SEC.
            for (GroupThreatOption const& o : bot->Kit().AreaThreat())
            {
                if (!o.spell)
                    continue;
                if (crowdControlNear && DealsDamage(o.spell))
                    continue;
                Unit* castTarget = o.onTarget ? victim : bot->me;
                if (!castTarget || !bot->CanCastWithStance(castTarget, o.spell))
                    continue;

                float const threat = o.taunt ? 0.0f : ThreatPerEnemy(bot, o, tankRate);
                std::pair<float, bool> const value = valueOf(threat, o.taunt ? TemporaryTauntSec(o.spell) : 0.0f);
                float cost = switchCost;
                if (bot->StanceSwapFor(o.spell) && !bot->CanTryToCastSpell(castTarget, o.spell))
                    cost += stanceCost;
                if (o.taunt)    // long cooldown: kept for a real emergency
                    cost += Value::Points(world, enemyMelee * ThreatBehavior::TAUNT_RESERVE_SEC) *
                            ThreatBehavior::GROUP_TAUNT_RESERVE_MULT;

                Proposal p = MakeProposal(value.second ? ProposalTier::Urgent : ProposalTier::Assigned,
                                          BotActionType::Cast, o.onTarget ? victim->GetObjectGuid() : ObjectGuid(), o.spell);
                p.reason = o.taunt ? "group taunt loose enemies" :
                           cost > switchCost ? "area threat on loose enemies (stance swap)" : "area threat on loose enemies";
                p.SetValue(value.first, cost);
                out.push_back(p);
            }
        }

        // Loss when `who` pulls `e` off its current victim for `seconds`: what `who` takes on top
        // of the victim (Value::MoveLoss). An ally holder has no member facts: all its hits count.
        float PullLoss(WorldModel const& world, EnemyFacts const& e, PartyMemberFacts const& who, float seconds)
        {
            if (PartyMemberFacts const* holder = world.FindMember(e.victim))
                return Value::MoveLoss(world, e, who, *holder, seconds);
            return Value::HitLoss(world, e, who, seconds);
        }

        float TauntMult(EnemyFacts const& e, bool* immune)
        {
            return LooseEnemy::Mult(e, immune);
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

    bool ThreatBehavior::TakesOver(Takeover what, PartyBotAI const* bot, WorldModel const& /*world*/) const
    {
        switch (what)
        {
            case Takeover::Taunt:
                return bot->GetRole() == ROLE_TANK && !bot->m_spellListTaunt.empty();
            case Takeover::ThreatDrop:
                return bot->GetRole() != ROLE_TANK && bot->Kit().ThreatDrop() && !bot->IsInDuel();
            default:
                return false;
        }
    }

    bool ThreatBehavior::WantsTickNow(PartyBotAI const* bot, WorldModel const& world, TeamPlan const& /*plan*/) const
    {
        if (bot->GetRole() != ROLE_TANK)
            return false;
        PartyMemberFacts const* tank = world.FindMember(bot->me->GetObjectGuid());
        if (!tank || !tank->CanAct())
            return false;
        for (EnemyFacts const& e : world.enemies)
        {
            if (!e.acting || e.crowdControlled || e.victim.IsEmpty() || e.victim == tank->guid || world.FindAlly(e.victim))
                continue;
            PartyMemberFacts const* victim = world.FindMember(e.victim);
            if (!victim || victim->charmed || Tanks::IsTank(*victim))
                continue;
            // With several tank bots each one wakes; Propose leaves the enemy to the nearest.
            if (PackResolver::Distance(tank->pos, e.pos) <= TANK_REACH_RANGE)
                return true;
        }
        return false;
    }

    void ThreatBehavior::Propose(PartyBotAI const* bot, WorldModel const& world,
                                 TeamPlan const& plan, std::vector<Proposal>& out) const
    {
        // Pet actions run besides the bot's own action (Arbiter), so both are proposed.
        ProposePet(bot, world, out);

        if (world.enemies.empty())
            return;

        if (bot->GetRole() != ROLE_TANK)
            ProposeThreatDrop(bot, world, out);

        switch (bot->GetRole())
        {
            case ROLE_TANK:
                ProposeTank(bot, world, plan, out);
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
    void ThreatBehavior::ProposeTank(PartyBotAI const* bot, WorldModel const& world, TeamPlan const& plan,
                                     std::vector<Proposal>& out) const
    {
        Player* me = bot->me;
        ObjectGuid const self = me->GetObjectGuid();
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
        // A warrior out of its stance for a moment (Thunder Clap, Taunt from Battle Stance).
        float const stanceCost = Value::LostAction(world, *myFacts, STANCE_SWAP_COST_SEC);

        // Enemies hitting someone else that the tank should consider for the group threat
        // proposal below: (enemy, gap, loss doing nothing, victim).
        struct LooseEntry
        {
            EnemyFacts const* enemy;
            PartyMemberFacts const* victim;
            float gap;
            float horizon;
            float lossNothing;
            float mult;
            bool deadly;
        };
        std::vector<LooseEntry> loose;

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
            if (Tanks::IsTank(*victim))
                continue;   // another tank (bot or player) holds it
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
            // Several tank bots: the nearest one takes a loose enemy, so two tanks do not run
            // after the same one and leave the rest (the others pick up their own).
            if (CloserTankBot(world, self, e.pos, distance))
                continue;

            bool tauntImmune = false;
            float const mult = TauntMult(e, &tauntImmune);

            // Losses compare with the tank taking the hits (Perception/LooseEnemy.h: MoveLoss +
            // HarassLoss). Winning it back with threat (walk over + recovery) is the brain's
            // choice since 2026-11-07 (TargetAssigner, Coordination/TankTargets.cpp); the same
            // values decide here whether a taunt is worth it.
            LooseValue const v = LooseEnemy::WinBack(world, e, *victim, *myFacts, tankRate, distance);
            float const gap = v.gap;
            float const horizon = v.horizon;
            float const lossNothing = v.lossNothing;
            bool const deadly = v.deadly;
            auto const lossFor = [&](float seconds) { return LooseEnemy::Loss(world, e, *victim, *myFacts, seconds); };
            ProposalTier const tier = deadly ? ProposalTier::Urgent : ProposalTier::Assigned;

            if (!tauntImmune)
            {
                uint32 lastTauntId = 0;
                for (SpellEntry const* pTaunt : bot->m_spellListTaunt)
                {
                    // Only taunts castable now (range, cooldown, rage, stance swap): an out of range
                    // or cooling down taunt only failed at execution. Duplicates listed once.
                    if (pTaunt->Id == lastTauntId || !bot->CanCastWithStance(unit, pTaunt))
                        continue;
                    lastTauntId = pTaunt->Id;
                    // Cooldown: Taunt and Mocking Blow use the category cooldown (Taunt 355 has
                    // RecoveryTime 0 and CategoryRecoveryTime 10 s).
                    uint32 const cooldownMs = pTaunt->GetRecoveryTime();
                    float const share = std::min(1.0f, (cooldownMs / 1000.0f) / LOOSE_HORIZON_SEC);
                    float const reserve = share * Value::Points(world, enemyMelee * TAUNT_RESERVE_SEC);
                    float const lossTaunt = lossFor(TauntSecOnVictim(gap, tankRate, TemporaryTauntSec(pTaunt), horizon));
                    float cost = switchCost + reserve;
                    if (bot->StanceSwapFor(pTaunt))
                        cost += stanceCost;
                    Proposal p = MakeProposal(tier, BotActionType::Cast, e.guid, pTaunt);
                    p.reason = "taunt loose enemy";
                    p.SetValue((lossNothing - lossTaunt) * mult, cost);
                    out.push_back(p);
                }
            }

            // Winning it back with threat (walking over, switching target): TargetAssigner.

            if (distance <= GROUP_THREAT_RADIUS)
                loose.push_back({ &e, victim, gap, horizon, lossNothing, mult, deadly });
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
            // No threat of its own yet (it came along with the one the tank pulled): any heal
            // takes it, whatever the tank's rate (a rate of 0 right after a ranged pull made
            // thinLead 0 and nothing counted, strat-20261107-143623).
            ThreatEntry const* own = ThreatView::FindEntry(e, self);
            bool const untouched = !own || own->threat <= 0.0f;
            if (ThreatView::VictimLead(e, &closest) >= thinLead && !untouched)
                continue;
            PartyMemberFacts const* runnerUp = closest.IsEmpty() ? nullptr : world.FindMember(closest);
            // Untouched with nobody else on its list yet: the healer's first heal puts it there.
            if (!runnerUp && untouched)
                for (PartyMemberFacts const& m : world.party)
                    if (m.alive && !m.isPet && m.role == ROLE_HEALER && m.guid != self)
                    {
                        runnerUp = &m;
                        break;
                    }
            if (runnerUp && !runnerUp->isPet)
                fragile.push_back({ &e, runnerUp });
        }

        // 1b. Several loose or fragile enemies around the tank (summoned adds, a wave arriving):
        // one area threat ability (Thunder Clap, Consecration, Challenging Roar) instead of one
        // switch per enemy per tick (combat scenarios 6 and 9).
        if (loose.size() + fragile.size() >= GROUP_THREAT_MIN_ENEMIES)
            ProposeGroupThreat(bot, world, plan, tankRate, switchCost, stanceCost, enemyMelee,
                [&](float threatAdded, float holdSec) -> std::pair<float, bool>
                {
                    float benefit = 0.0f;
                    bool deadly = false;
                    // A fragile enemy would turn on the runner-up for a moment; the area ability
                    // makes that less likely (counted at FRAGILE_WEIGHT of that loss), in
                    // proportion to how much of the missing lead it adds: one Demoralizing Shout
                    // (56) does not make a lead of one global cooldown, and counting it in full
                    // spent the tank's rage on shouts (strat-20261101-210522).
                    float const fragileShare = holdSec > 0.0f ? 1.0f :
                        (thinLead > 0.0f ? std::min(1.0f, threatAdded / thinLead) : (threatAdded > 0.0f ? 1.0f : 0.0f));
                    if (fragileShare > 0.0f)
                        for (FragileEnemy const& f : fragile)
                            benefit += Value::MoveLoss(world, *f.enemy, *f.runnerUp, *myFacts, TANK_RECLAIM_SEC) *
                                       FRAGILE_WEIGHT * fragileShare;
                    for (LooseEntry const& l : loose)
                    {
                        float sec;
                        if (holdSec > 0.0f)     // area taunt: back on the victim when it fades
                            sec = TauntSecOnVictim(l.gap, tankRate, holdSec, l.horizon);
                        else
                        {
                            float const rest = std::max(0.0f, l.gap - threatAdded);
                            float const recovery = tankRate > 1.0f ? rest / tankRate : ThreatView::NO_LIMIT;
                            sec = std::min(recovery, l.horizon);
                        }
                        // Same loss as l.lossNothing (damage on top of the tank + harassment).
                        float const loss = Value::MoveLoss(world, *l.enemy, *l.victim, *myFacts, sec) +
                                           Value::HarassLoss(world, *l.enemy, *l.victim, sec);
                        benefit += (l.lossNothing - loss) * l.mult;
                        deadly = deadly || l.deadly;
                    }
                    return { benefit, deadly };
                }, out);

        // 2. Topping up the enemy with the smallest lead (a target switch): TargetAssigner.
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
        // Only what I would take on top of the holder (Value::MoveLoss): pulling a caster costs
        // nothing but the death risk, its spells hit the holder just as hard.
        float const ttd = Value::EnemyTimeToDie(world, *e);
        float const avoided = PullLoss(world, *e, *myFacts, std::min(TANK_RECLAIM_SEC, ttd));

        // Switching to a safe enemy instead is the brain's (TargetAssigner: its stop rule is the
        // same ThreatRoom::Stop, so the next refresh gives this bot another target if one is
        // safe); until then stop hitting.
        Proposal p = MakeProposal(ProposalTier::Assigned, BotActionType::HoldFire);
        p.reason = "near aggro, stop";
        p.SetValue(avoided, Value::LostAction(world, *myFacts, tickSec));
        out.push_back(p);
    }

    float ThreatBehavior::NeededHeadroom(WorldModel const& world, ObjectGuid who, EnemyFacts const& enemy) const
    {
        // Perception/ThreatRoom.h (shared with TargetAssigner).
        return ThreatRoom::Stop(world, who, enemy, sCommanderMgr.GetBotUpdateInterval() / 1000.0f);
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
        // The tank roles do not flip mid-fight (Tanks.h), so no stance-based hysteresis is needed.
        bool const tank = HasTank(world);
        std::vector<SpellEntry const*> taunts;
        PetTauntSpells(pet, taunts);
        for (SpellEntry const* spell : taunts)
        {
            bool const autocast = std::find(pet->m_autospells.begin(), pet->m_autospells.end(), spell->Id) !=
                                  pet->m_autospells.end();
            bool const turnOff = autocast && tank;
            bool const turnOn = !autocast && !tank;
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

        PartyMemberFacts const* tankBot = Tanks::Main(world, true);

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
        float const avoided = holding ? Value::HitLoss(world, *e, *petFacts, std::min(reclaimSec, ttd)) :
                                        PullLoss(world, *e, *petFacts, std::min(reclaimSec, ttd));

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
    float ThreatBehavior::HealPullLoss(PartyBotAI const* bot, WorldModel const& world, Unit* target,
                                       SpellEntry const* spell, float* effectiveOut)
    {
        Player* me = bot->me;
        ObjectGuid const self = me->GetObjectGuid();
        PartyMemberFacts const* myFacts = world.FindMember(self);
        if (!myFacts)
            return 0.0f;

        // Health missing when the heal lands, the same estimate CastWatch checks mid-cast.
        // With the missing health of now, the arbiter approved heals that CastWatch cancelled
        // 0.3 s later and the rotation started again (9 of 24 Heals in the second Stockade
        // test, 2026-11-01).
        float const missing = float(target->GetMaxHealth() - target->GetHealth());
        float const castSec = float(spell->GetCastTime(me)) / 1000.0f;
        float const atLanding = std::min(float(target->GetMaxHealth()),
                                         missing + Value::IncomingOn(world, target->GetObjectGuid()) * castSec);
        float const effective = std::min(atLanding, HealSpells::Estimate(me, target, spell).direct);
        if (effectiveOut)
            *effectiveOut = effective;
        std::vector<ThreatView::EnemyThreatDelta> deltas;
        ThreatView::PredictHealThreat(me, target, effective, spell, deltas);

        float loss = 0.0f;
        float unknownEnemyLoss = -1.0f;     // computed on first use
        for (ThreatView::EnemyThreatDelta const& d : deltas)
        {
            if (d.added <= 0.0f)
                continue;
            EnemyFacts const* e = world.FindEnemy(d.enemy);
            if (!e)
            {
                // On the live threat refs but not in the snapshot yet: an add summoned since
                // the last refresh, with 0 threat on everyone. Any heal pulls it (scenario 6).
                // Assume it hits like the average acting enemy.
                if (unknownEnemyLoss < 0.0f)
                {
                    unknownEnemyLoss = 0.0f;
                    uint32 count = 0;
                    for (EnemyFacts const& a : world.enemies)
                    {
                        if (!a.acting)
                            continue;
                        unknownEnemyLoss += Value::HitLoss(world, a, *myFacts, TANK_RECLAIM_SEC);
                        ++count;
                    }
                    unknownEnemyLoss = count ? unknownEnemyLoss / float(count) : 0.0f;
                }
                loss += unknownEnemyLoss;
                continue;
            }
            if (ThreatView::Headroom(*e, self) - d.added >= 0.0f)
                continue;
            float const ttd = Value::EnemyTimeToDie(world, *e);
            loss += PullLoss(world, *e, *myFacts, std::min(TANK_RECLAIM_SEC, ttd));
        }
        return loss;
    }

    void ThreatBehavior::ProposeHealer(PartyBotAI const* bot, WorldModel const& world, std::vector<Proposal>& out) const
    {
        Player* me = bot->me;
        // The heal module decides heals itself and checks aggro with HealPullLoss.
        if (HealPlanBehavior::ControlsHealingOf(bot, world))
            return;

        Unit* pTarget = bot->SelectHealTarget();     // same call as FindAndHealInjuredAlly()
        if (!pTarget)
            return;
        if (pTarget->GetHealthPercent() < HEAL_IGNORE_THREAT_PCT)
            return;

        // The heal the rotation would cast (CombatBotBaseAI::HealInjuredTarget). A pure HoT:
        // its threat comes in ticks, left alone.
        HealChoice choice;
        if (!bot->ChooseHeal(pTarget, choice) || choice.Chosen().amount.direct <= 0.0f)
            return;

        ObjectGuid const self = me->GetObjectGuid();
        PartyMemberFacts const* myFacts = world.FindMember(self);
        PartyMemberFacts const* targetFacts = world.FindMember(pTarget->GetObjectGuid());
        if (!myFacts || !targetFacts)
            return;

        float const missing = float(pTarget->GetMaxHealth() - pTarget->GetHealth());

        // Points lost to enemies that turn on me after this heal on `target` (0 = safe).
        auto aggroLoss = [&](Unit* target, float /*targetMissing*/, SpellEntry const* spell, float* effectiveOut)
        {
            return HealPullLoss(bot, world, target, spell, effectiveOut);
        };

        // The chosen heal first, then the direct heals smaller than it, biggest first
        // (HealChoice::options is sorted by direct heal).
        auto chosenThenSmaller = [](HealChoice const& c, std::vector<SpellEntry const*>& out)
        {
            HealSpells::Option const& chosen = c.Chosen();
            out.push_back(chosen.spell);
            for (HealSpells::Option const& o : c.options)
                if (o.spell != chosen.spell && o.amount.direct > 0.0f && o.amount.direct < chosen.amount.direct)
                    out.push_back(o.spell);
        };

        std::vector<SpellEntry const*> castable;
        chosenThenSmaller(choice, castable);
        SpellEntry const* predicted = castable.front();
        float predictedHeal = 0.0f;
        float const loss = aggroLoss(pTarget, missing, predicted, &predictedHeal);
        if (loss <= 0.0f)
            return;

        // Option 1: a smaller heal that does not pull. Costs the healing it lacks.
        for (size_t i = 1; i < castable.size(); ++i)
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
        // mana is short (tension > 1), only members below their heal ceiling. The rank is the
        // one ChooseHeal picks (mana efficient), or a smaller one.
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
            HealChoice otherChoice;
            // A direct heal only: this replaces the rotation's cast, and a HoT's threat is not
            // what aggroLoss measures.
            if (bot->ChooseHeal(other, otherChoice, true))
            {
                // The rank ChooseHeal picks for that member (already the most efficient one that
                // keeps up), then smaller ones, until one is safe. CanTryToCastSpell (ChooseHeal)
                // already left out heals the bot cannot pay for.
                std::vector<SpellEntry const*> otherCastable;
                chosenThenSmaller(otherChoice, otherCastable);
                for (SpellEntry const* spell : otherCastable)
                {
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

    void ThreatBehavior::ProposeThreatDrop(PartyBotAI const* bot, WorldModel const& world, std::vector<Proposal>& out) const
    {
        SpellEntry const* spell = bot->Kit().ThreatDrop();
        Player* me = bot->me;
        if (!spell || !bot->CanTryToCastSpell(me, spell))
            return;
        ObjectGuid const self = me->GetObjectGuid();
        PartyMemberFacts const* myFacts = world.FindMember(self);
        if (!myFacts)
            return;

        // Fade: SPELL_AURA_MOD_TOTAL_THREAT with a negative amount on every threat list, for the
        // aura's duration (ThreatManager::setTempThreatModifier, never below 0).
        float const drop = -float(spell->CalculateSimpleValue(EFFECT_INDEX_0));
        float const durationSec = std::max(0, spell->GetDuration()) / 1000.0f;
        if (drop <= 0.0f || durationSec <= 0.0f)
            return;

        float saved = 0.0f;
        for (EnemyFacts const& e : world.enemies)
        {
            if (!e.acting || e.crowdControlled || e.victim != self)
                continue;
            float mine = 0.0f;
            for (ThreatEntry const& r : e.threat)
                if (r.guid == self)
                    mine = r.threat;
            float const after = std::max(0.0f, mine - drop);
            // Who it turns to: the highest other row that beats my lowered threat by the
            // aggro rule (110% in melee reach, 130% otherwise). Nobody: Fade changes nothing here.
            ThreatEntry const* next = nullptr;
            for (ThreatEntry const& r : e.threat)
            {
                if (r.guid == self || !r.onList)
                    continue;
                if (r.threat > after * (r.inMeleeReach ? 1.1f : 1.3f) && (!next || r.threat > next->threat))
                    next = &r;
            }
            if (!next)
                continue;
            // Hits I do not take while it hits `next` instead (only what I would take on top),
            // until Fade ends or it dies. An ally or another unit: all of my hits count.
            float const seconds = std::min(durationSec, Value::EnemyTimeToDie(world, e));
            if (PartyMemberFacts const* holder = world.FindMember(next->guid))
                saved += std::max(0.0f, Value::MoveLoss(world, e, *myFacts, *holder, seconds));
            else
                saved += Value::HitLoss(world, e, *myFacts, seconds);
        }
        if (saved <= 0.0f)
            return;

        // About to die: as urgent as the emergency heal it competes with.
        float const incoming = Value::IncomingOn(world, self);
        bool const dying = incoming > 0.0f && float(myFacts->health) / incoming < FADE_URGENT_SEC;
        Proposal p = MakeProposal(dying ? ProposalTier::Urgent : ProposalTier::Assigned, BotActionType::Cast, self, spell);
        p.reason = "attacked: fade";
        p.SetValue(saved, Value::LostAction(world, *myFacts, Value::GCD_SEC));
        out.push_back(p);
    }
}