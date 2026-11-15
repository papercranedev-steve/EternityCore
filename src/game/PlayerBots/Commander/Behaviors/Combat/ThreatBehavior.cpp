#include "Commander/Behaviors/Combat/ThreatBehavior.h"
#include "Commander/Behaviors/Support/ManaBudgetBehavior.h"
#include "Commander/Behaviors/Support/HealPlanBehavior.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/Forecast/ManaBudget.h"
#include "Commander/Perception/Forecast/ThreatView.h"
#include "Commander/Perception/Spatial/PackResolver.h"
#include "Commander/Perception/Values/ValueModel.h"
#include "Commander/Perception/Profiles/ProfileStore.h"
#include "Commander/Perception/Tanks.h"
#include "Commander/Perception/Values/LooseEnemy.h"
#include "Commander/Perception/Forecast/ThreatRoom.h"
#include "Commander/Perception/Values/ThreatRisk.h"
#include "Commander/Perception/Profiles/PetTaunt.h"
#include "Commander/Perception/Profiles/PetAbilities.h"
#include "Commander/Perception/Values/RescueValue.h"
#include "Commander/Perception/Profiles/SpellTags.h"
#include "Commander/CommanderMgr.h"
#include "Commander/Classes/ClassKit.h"
#include "PartyBotAI.h"
#include "HealSpells.h"
#include "DBCStores.h"
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
            return PetTaunt::HasAutocast(pet);
        }

        // `victim` holds the enemy it is being hit by: a holder member (above), a holder pet
        // (IsHolderPet), or a tank-like ally (AllyFacts::tankLike, e.g. Sergeant Bly in
        // Zul'Farrak, combat scenario 9).
        // Damage dealers leave room under its threat the same way. Threat rows of allies are
        // on EnemyFacts::threat (partySide = false), so ThreatView::Headroom works unchanged:
        // it compares my threat with victimThreat, whoever the victim is.
        // An off-tank holding `e` (doctrine OffTank.h: the hunter's pet besides the main tank)
        // is its holder too.
        bool IsHeldByHolder(PartyBotAI const* bot, WorldModel const& world, TeamPlan const& plan, EnemyFacts const& e)
        {
            ObjectGuid const victim = e.victim;
            if (plan.doctrine.OffTankHolds(victim, e.guid))
                return true;
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
        // An off-tank's enemy is killed last (doctrine OffTank.h): never a switch target.
        EnemyFacts const* FindSafeTarget(PartyBotAI const* bot, WorldModel const& world, TeamPlan const& plan,
                                         ObjectGuid who, Position3 const& from, ObjectGuid current, ObjectGuid preferred)
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
                if (!IsHeldByHolder(bot, world, plan, a))
                    continue;   // switching onto an enemy that is already on a healer does not help
                if (plan.doctrine.Has(a.guid, STAMP_OFF_TANK))
                    continue;
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


        // One proposal per usable area threat ability. `valueOf(threatAdded, holdSec, centre,
        // radius)` returns the benefit over the enemies it reaches and whether someone would die
        // (holdSec > 0: an area taunt holding for that long, TemporaryTauntSec; centre and
        // radius: where the spell lands and its area, SpellTags::AreaRadius).
        template <typename ValueFn>
        void ProposeGroupThreat(PartyBotAI const* bot, WorldModel const& world, TeamPlan const& plan, float tankRate,
                                float switchCost, float stanceCost, float enemyMelee, bool protectsCaster,
                                ValueFn valueOf, std::vector<Proposal>& out)
        {
            Unit* victim = bot->me->GetVictim();
            // Damage breaks crowd control (Polymorph, Sap, Shackle) on enemies around the tank:
            // the plan's constraint (TeamPlan::AreaKeepsUnbroken: breakable controls on them, or
            // about to be put on them). A stun or fear that damage does not end is no reason.
            bool crowdControlNear = false;
            Position3 myPos;
            myPos.x = bot->me->GetPositionX();
            myPos.y = bot->me->GetPositionY();
            myPos.z = bot->me->GetPositionZ();
            for (EnemyFacts const& e : world.enemies)
                crowdControlNear = crowdControlNear ||
                    (plan.AreaKeepsUnbroken(e) && PackResolver::Distance(myPos, e.pos) <= ThreatBehavior::GROUP_THREAT_RADIUS);
            // A warrior's Thunder Clap from Defensive Stance is cast after a stance swap
            // (Arbiter -> PartyBotAI::DoCastWithStance), costed with STANCE_SWAP_COST_SEC.
            for (GroupThreatOption const& o : bot->Kit().AreaThreat())
            {
                if (!o.spell)
                    continue;
                if (crowdControlNear && SpellTags::DealsDamage(o.spell))
                    continue;
                Unit* castTarget = o.onTarget ? victim : bot->me;
                if (!castTarget || !bot->CanCastWithStance(castTarget, o.spell))
                    continue;

                float const threat = o.taunt ? 0.0f : ThreatPerEnemy(bot, o, tankRate);
                Position3 centre;
                centre.x = castTarget->GetPositionX();
                centre.y = castTarget->GetPositionY();
                centre.z = castTarget->GetPositionZ();
                float const area = SpellTags::AreaRadius(o.spell);
                std::pair<float, bool> const value = valueOf(threat, o.taunt ? RescueValue::TemporaryTauntSec(o.spell) : 0.0f,
                                                             centre, area > 0.0f ? area : ThreatBehavior::GROUP_THREAT_RADIUS);
                float cost = switchCost;
                if (bot->StanceSwapFor(o.spell) && !bot->CanTryToCastSpell(castTarget, o.spell))
                    cost += stanceCost;
                // Long cooldown: kept for a real emergency. Several enemies turned onto the area
                // caster the doctrine has the tank protect (HoldPile) is that emergency: no reserve.
                if (o.taunt && !protectsCaster)
                    cost += Value::Points(world, enemyMelee * ThreatBehavior::TAUNT_RESERVE_SEC) *
                            ThreatBehavior::GROUP_TAUNT_RESERVE_MULT;

                Proposal p = MakeProposal(value.second || protectsCaster ? ProposalTier::Urgent : ProposalTier::Assigned,
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

    void ThreatBehavior::Propose(PartyBotAI const* bot, WorldModel const& world,
                                 TeamPlan const& plan, std::vector<Proposal>& out) const
    {
        // Pet actions run besides the bot's own action (Arbiter), so both are proposed.
        ProposePet(bot, world, plan, out);

        if (world.enemies.empty())
            return;

        // Doctrine HoldFire (RoleStamps.h): its damage shield / reflection is up; every role stops
        // hitting it (TargetAssigner gives another target when there is one).
        // Another target given: TargetBehavior switches to it, no need to stop first.
        Unit const* victim = bot->me->GetVictim();
        TargetDuty const* next = plan.FindTargetFor(bot->me->GetObjectGuid());
        bool const switching = victim && next && !next->enemy.IsEmpty() && next->enemy != victim->GetObjectGuid();
        if (victim && !switching && plan.doctrine.Has(victim->GetObjectGuid(), STAMP_HOLD_FIRE))
        {
            Proposal p = MakeProposal(ProposalTier::Urgent, BotActionType::HoldFire);
            p.reason = "it reflects damage: hands off";
            p.score = 1.0f;
            out.push_back(p);
        }

        switch (bot->GetRole())
        {
            case ROLE_TANK:
                ProposeTank(bot, world, plan, out);
                break;
            case ROLE_MELEE_DPS:
            case ROLE_RANGE_DPS:
                ProposeDamage(bot, world, plan, out);
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
        float const tankRate = LooseEnemy::TankRate(world, self);
        // An area taunt used now is not there for the next group that breaks loose.
        float const enemyMelee = RescueValue::EnemyDps(world);
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
            float mult;
            bool deadly;
        };
        std::vector<LooseEntry> loose;

        // 1. Loose enemies hitting someone who is not a tank.
        for (EnemyFacts const& e : world.enemies)
        {
            // Loose: on a member who is not a tank, not an ally's fight, not a pet that growls
            // it back (Perception/Values/RescueValue.h, shared with the brain's rescue). Enemies on
            // pets are loose too: with a tank around, the pet's taunt autocast is off.
            PartyMemberFacts const* victim = RescueValue::LooseVictim(world, e);
            if (!victim || plan.doctrine.Has(e.guid, STAMP_SELF_DESTRUCT))   // dies anyway (RoleStamps.h)
                continue;
            // On the off-tank holding it (doctrine OffTank.h): held.
            if (plan.doctrine.OffTankHolds(victim->guid, e.guid))
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

            float const mult = LooseEnemy::Mult(e, nullptr);
            // Taking one loose enemy off its victim (taunt, winning it back with threat, the
            // victim's Fade) is the brain's since 2026-11-07 (team rescue: RescueAssigner,
            // RescueBehavior casts the taunt). Here only the area threat that takes several.
            // One the brain gave somebody else (the priest fades it) is not counted again.
            RescueDuty const* rescue = nullptr;
            for (RescueDuty const& d : plan.rescues)
                if (d.enemy == e.guid)
                    rescue = &d;
            if (rescue && rescue->bot != self)
                continue;
            LooseValue const v = LooseEnemy::WinBack(world, e, *victim, *myFacts, tankRate, distance);

            if (distance <= GROUP_THREAT_RADIUS)
                loose.push_back({ &e, victim, v.gap, v.horizon, mult, v.deadly });
        }

        // Doctrine HoldPile (Area tactic, TankJob): two or more enemies turned onto the area caster
        // make the group threat (Challenging Shout) Urgent; its worth is still the points below.
        bool const protects = plan.doctrine.tankJob == TankJob::HoldPile && plan.area.Active() &&
            std::count_if(loose.begin(), loose.end(),
                [&plan](LooseEntry const& l) { return plan.area.CastBy(l.victim->guid); }) >= 2;

        // 1b. Enemies still on the tank at risk of being taken off it (Perception/Values/ThreatRisk.h,
        // with the area spell the doctrine counts on; user 2026-11-13, doc
        // .kiro/docs/2026-11-13-tank-threat-priority.md 5): the threat a cast adds on each is
        // worth the risk it takes off, the tank's single target value too (TankTargets). Covers
        // the start of a fight (every enemy at 0 tank threat, a healer's next heal) without a
        // separate opening (until 2026-11-13: the threat opening's gaps x the waiting party's
        // time, and thin runways x ThinLoss).
        struct RiskEntry
        {
            EnemyFacts const* enemy;
            float risk;
        };
        std::vector<RiskEntry> risky;
        float const horizon = plan.doctrine.RunwayHorizon();
        for (EnemyFacts const& e : world.enemies)
        {
            if (!ThreatRisk::Counts(e, self) ||
                PackResolver::Distance(myFacts->pos, e.pos) - e.boundingRadius > GROUP_THREAT_RADIUS)
                continue;
            float const risk = ThreatRisk::Of(world, e, self, plan.doctrine.AreaThreatOn(e.guid), horizon);
            if (risk > 0.0f)
                risky.push_back({ &e, risk });
        }

        // Several loose enemies (summoned adds, a wave arriving) or several at risk: one area
        // threat ability (Thunder Clap, Demoralizing Shout, Consecration, Challenging Roar)
        // instead of one switch per enemy per tick (combat scenarios 6 and 9).
        if (loose.size() >= GROUP_THREAT_MIN_ENEMIES || risky.size() >= GROUP_THREAT_MIN_ENEMIES)
            ProposeGroupThreat(bot, world, plan, tankRate, switchCost, stanceCost, enemyMelee, protects,
                [&](float threatAdded, float holdSec, Position3 const& centre, float radius) -> std::pair<float, bool>
                {
                    float benefit = 0.0f;
                    bool deadly = false;
                    if (threatAdded > 0.0f)
                        for (RiskEntry const& r : risky)
                        {
                            if (PackResolver::Distance(centre, r.enemy->pos) - r.enemy->boundingRadius > radius)
                                continue;
                            float const after = ThreatRisk::Of(world, *r.enemy, self, plan.doctrine.AreaThreatOn(r.enemy->guid),
                                                               horizon, threatAdded);
                            benefit += std::max(0.0f, r.risk - after);
                        }
                    // Loose enemies: without the ability the tank wins them back with threat one
                    // after another (LooseEnemy::SequentialRecovery); with it each lacks
                    // `threatAdded` less (an area taunt: back on the victim when it fades). The
                    // benefit is the difference. Before 2026-11-08 the baseline was "nobody ever
                    // wins it back" and every enemy recovered at the full rate at once after the
                    // shout, so the shout was credited with the tank's whole threat rate on
                    // every enemy: 11 Demoralizing Shouts in 30 s (strat-20261108-141921).
                    std::vector<float> gaps;
                    std::vector<float> rests;
                    for (LooseEntry const& l : loose)
                    {
                        gaps.push_back(l.gap);
                        rests.push_back(std::max(0.0f, l.gap - threatAdded));
                    }
                    std::vector<float> const before = LooseEnemy::SequentialRecovery(gaps, tankRate);
                    std::vector<float> const after = LooseEnemy::SequentialRecovery(rests, tankRate);
                    for (size_t i = 0; i < loose.size(); ++i)
                    {
                        LooseEntry const& l = loose[i];
                        float const secBefore = std::min(before[i], l.horizon);
                        float const secAfter = holdSec > 0.0f
                            ? RescueValue::TauntSecOnVictim(l.gap, tankRate, holdSec, l.horizon)
                            : std::min(after[i], l.horizon);
                        // Damage on top of the tank + harassment (LooseEnemy::Loss).
                        float const lossBefore = LooseEnemy::Loss(world, *l.enemy, *l.victim, *myFacts, secBefore);
                        float const lossAfter = LooseEnemy::Loss(world, *l.enemy, *l.victim, *myFacts, secAfter);
                        benefit += std::max(0.0f, lossBefore - lossAfter) * l.mult;
                        deadly = deadly || l.deadly;
                    }
                    return { benefit, deadly };
                }, out);

        // 2. Which enemy the tank hits (the same risk, one at a time): TargetAssigner (TankTargets).
    }

    // Damage dealers: stop before pulling aggro off a holder, when it is worth it.
    void ThreatBehavior::ProposeDamage(PartyBotAI const* bot, WorldModel const& world, TeamPlan const& plan,
                                       std::vector<Proposal>& out) const
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
        // Doctrine NoHold (fodder, a trap swarm): the tank need not hold it (DamageTargets).
        if (plan.doctrine.Has(e->guid, STAMP_NO_HOLD))
            return;

        // Only protect a holder (also a tank-like ally). If it is on a healer, another dps or an
        // ally that is not tank-like, taking it is not worse.
        if (!IsHeldByHolder(bot, world, plan, *e))
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
        // Perception/Forecast/ThreatRoom.h (shared with TargetAssigner).
        return ThreatRoom::Stop(world, who, enemy, sCommanderMgr.GetBotUpdateInterval() / 1000.0f);
    }

    namespace
    {
        // A pet's area damage (Thunderstomp) reaches past its radius from where the pet stands
        // now: it moves with its victim.
        constexpr float PET_AREA_MARGIN_YD = 2.0f;
        // ChargeMovementGenerator: four times the run speed, at most 24 yd/s.
        constexpr float CHARGE_MAX_SPEED = 24.0f;

        // The pet's area damage on autocast would wake a control that must hold (the bots' own
        // area spells' rule, TeamPlan::AreaKeepsUnbroken, as Constraints' KeptNear): PetAI casts
        // its autocasts without looking.
        bool PetAreaWakes(WorldModel const& world, TeamPlan const& plan, PartyMemberFacts const& pet, SpellEntry const* spell)
        {
            float const radius = SpellTags::AreaRadius(spell) + PET_AREA_MARGIN_YD;
            uint32 const now = WorldTimer::getMSTime();
            for (EnemyFacts const& e : world.enemies)
            {
                if (!e.acting || PackResolver::Distance(pet.pos, e.pos) - e.boundingRadius > radius)
                    continue;
                if (plan.AreaKeepsUnbroken(e) && !plan.controlEnd.Breaks(pet.guid, e.guid, now))
                    return true;
            }
            return false;
        }

        bool PetCanCast(Pet* pet, SpellEntry const* spell)
        {
            Powers const power = Powers(spell->powerType);
            return pet->IsSpellReady(spell) && !pet->HasGCD(spell) &&
                   (power == POWER_HEALTH || pet->GetPower(power) >= Spell::CalculatePowerCost(spell, pet));
        }

        // Seconds `gap` yards take at `run` yd/s with `pct` more speed for its first `sec` seconds.
        float SpedUpSec(float gap, float run, float pct, float sec)
        {
            float const fast = run * (1.0f + pct);
            return gap <= fast * sec ? gap / fast : sec + (gap - fast * sec) / run;
        }

        // Charge or Dash on the pet's way to the enemy it must take (an off-tank's job, user
        // 2026-11-15): arriving sooner saves the enemy's hits on its victim meanwhile
        // (Value::HitLoss over the seconds saved); the cost is the spell's cooldown.
        void ProposePetReach(WorldModel const& world, TeamPlan const& plan, Pet* pet,
                             PartyMemberFacts const& petFacts, EnemyFacts const& job, std::vector<Proposal>& out)
        {
            Unit* enemy = pet->GetMap()->GetUnit(job.guid);
            PartyMemberFacts const* hurt = world.FindMember(job.victim);
            if (!enemy || !enemy->IsAlive() || !hurt || hurt->guid == petFacts.guid ||
                pet->CanReachWithMeleeAutoAttack(enemy) || pet->HasUnitState(UNIT_STATE_ROOT))
                return;
            float const gap = pet->GetDistance(enemy);
            float const run = pet->GetSpeed(MOVE_RUN);
            if (run <= 0.0f)
                return;
            float const walkSec = gap / run;
            float const horizon = Value::FightRemainingSec(world);

            if (SpellEntry const* charge = PetAbilities::First(pet, PetAbilities::Kind::Charge))
            {
                SpellRangeEntry const* range = sSpellRangeStore.LookupEntry(charge->rangeIndex);
                if (range && gap >= range->minRange && gap <= range->maxRange && PetCanCast(pet, charge) &&
                    !plan.ForbidsGapCloser(petFacts.guid) && pet->IsWithinLOSInMap(enemy))
                {
                    float const saved = walkSec - gap / std::min(run * 4.0f, CHARGE_MAX_SPEED);
                    float const benefit = Value::HitLoss(world, job, *hurt, saved);
                    float const cost = Value::CooldownPoints(charge->GetRecoveryTime() / 1000.0f, horizon);
                    if (benefit > cost)
                    {
                        Proposal p = MakeProposal(ProposalTier::Assigned, BotActionType::PetCast, job.guid, charge);
                        p.reason = "off-tank: charge its enemy";
                        p.SetValue(benefit, cost);
                        out.push_back(p);
                        return;
                    }
                }
            }

            if (SpellEntry const* dash = PetAbilities::First(pet, PetAbilities::Kind::Speed))
            {
                if (!PetCanCast(pet, dash) || pet->HasAura(dash->Id))
                    return;
                float pct = 0.0f;
                for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
                    if (dash->EffectApplyAuraName[i] == SPELL_AURA_MOD_INCREASE_SPEED)
                        pct = std::max(pct, dash->CalculateSimpleValue(SpellEffectIndex(i)) / 100.0f);
                float const sec = std::max(0, dash->GetDuration()) / 1000.0f;
                float const saved = walkSec - SpedUpSec(gap, run, pct, sec);
                float const benefit = Value::HitLoss(world, job, *hurt, saved);
                float const cost = Value::CooldownPoints(dash->GetRecoveryTime() / 1000.0f, horizon);
                if (pct <= 0.0f || benefit <= cost)
                    return;
                Proposal p = MakeProposal(ProposalTier::Assigned, BotActionType::PetCast, ObjectGuid(), dash);
                p.reason = "off-tank: dash to its enemy";
                p.SetValue(benefit, cost);
                out.push_back(p);
            }
        }
    }

    // Pet of this bot (hunter, warlock): threat-behavior doc 4.7.
    void ThreatBehavior::ProposePet(PartyBotAI const* bot, WorldModel const& world, TeamPlan const& plan,
                                    std::vector<Proposal>& out) const
    {
        Player* me = bot->me;
        Pet* pet = me->GetPet();
        if (!pet || !pet->IsAlive() || !pet->IsControlled() || pet->IsCharmed() || !pet->GetCharmInfo())
            return;
        ObjectGuid const petGuid = pet->GetObjectGuid();
        PartyMemberFacts const* petFacts = world.FindMember(petGuid);
        if (!petFacts)
            return;
        // A pet kept out of the fight (ClassKit::PetStandAside: the phased Imp) is never sent in.
        if (SpellEntry const* aside = bot->Kit().PetStandAside())
            if (pet->HasSpell(aside->Id))
                return;

        // An off-tank's enemy (doctrine OffTank.h): the one the pet holds, else the loose enemy
        // the brain's rescue gives it to win with threat (RescueAssigner).
        EnemyFacts const* job = world.FindEnemy(plan.doctrine.OffTankEnemyOf(petGuid));
        RescueDuty const* rescue = plan.FindRescueFor(petGuid);
        if (!job && rescue && rescue->way == RescueWay::Threat)
            job = world.FindEnemy(rescue->enemy);

        // Taunt autocast (Growl, Torment, Suffering): off while a tank holds the enemies, on
        // again without one, and on while the pet off-tanks (it casts it on its own victim:
        // the job's enemy). Proposed only while it differs, so once per change.
        // The tank roles do not flip mid-fight (Tanks.h), so no stance-based hysteresis is needed.
        bool const tank = HasTank(world);
        bool const tauntOn = !tank || job;
        std::vector<SpellEntry const*> taunts;
        PetTaunt::Spells(pet, taunts);
        for (SpellEntry const* spell : taunts)
        {
            bool const autocast = std::find(pet->m_autospells.begin(), pet->m_autospells.end(), spell->Id) !=
                                  pet->m_autospells.end();
            bool const turnOff = autocast && !tauntOn;
            bool const turnOn = !autocast && tauntOn;
            if (!turnOff && !turnOn)
                continue;
            Proposal p = MakeProposal(ProposalTier::Assigned,
                turnOff ? BotActionType::PetAutocastOff : BotActionType::PetAutocastOn, ObjectGuid(), spell);
            p.reason = turnOff ? "tank holds, pet taunt off" : job ? "off-tank, pet taunt on" : "no tank, pet taunt on";
            out.push_back(p);
        }

        // Area damage autocast (Thunderstomp, user 2026-11-15): off while a control that must
        // hold is within its reach of the pet, on again once none is (CombatBotBaseAI::
        // TrainHunterPet turns it on).
        std::vector<SpellEntry const*> areas;
        PetAbilities::Learned(pet, PetAbilities::Kind::AreaDamage, areas);
        for (SpellEntry const* spell : areas)
        {
            if (!spell->IsAutocastable())
                continue;
            bool const wakes = PetAreaWakes(world, plan, *petFacts, spell);
            if (PetAbilities::IsAutocast(pet, spell) != wakes)
                continue;
            Proposal p = MakeProposal(ProposalTier::Assigned,
                wakes ? BotActionType::PetAutocastOff : BotActionType::PetAutocastOn, ObjectGuid(), spell);
            p.reason = wakes ? "control near, pet area damage off" : "no control near, pet area damage on";
            out.push_back(p);
        }

        if (world.enemies.empty())
            return;

        // Off-tank: on its enemy and nothing else (the switch and stop rules below are for a pet
        // that only adds damage). Charge or Dash on the way there.
        if (job)
        {
            ProposePetReach(world, plan, pet, *petFacts, *job, out);
            Unit const* victim = pet->GetVictim();
            if (victim && victim->GetObjectGuid() == job->guid)
                return;
            float const tickSec = sCommanderMgr.GetBotUpdateInterval() / 1000.0f;
            Proposal p = MakeProposal(ProposalTier::Assigned, BotActionType::PetAttack, job->guid);
            p.reason = rescue && rescue->enemy == job->guid ? "off-tank: win the loose enemy" : "off-tank: back on its enemy";
            // Benefit: the rescue's worth (taking the enemy off its victim), else what holding it
            // is worth to the doctrine; at least the pet's damage over the next ticks.
            OffTankHold const* held = plan.doctrine.OffTankOf(job->guid);
            float const worth = rescue && rescue->enemy == job->guid ? rescue->points : held ? held->points : 0.0f;
            p.SetValue(std::max({ 1.0f, worth, Value::LostAction(world, *petFacts, 2.0f * tickSec) }), 0.0f);
            out.push_back(p);
            return;
        }
        // No tank and the pet taunts by itself: it holds enemies (IsHolderPet), leave it.
        if (!tank && PetTaunt::HasAutocast(pet))
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
            bool const safe = !IsHeldByHolder(bot, world, plan, *e) ||
                (e->victimThreat > 0.0f && room / e->victimThreat >= minRatio &&
                 room >= OwnThreatRate(world, petGuid, *e) * minSec);
            EnemyFacts const* sendAt = safe ? e : FindSafeTarget(bot, world, plan, petGuid, petFacts->pos, e->guid,
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
        else if (!IsHeldByHolder(bot, world, plan, *e) ||
                 ThreatView::Headroom(*e, petGuid) >= NeededHeadroom(world, petGuid, *e))
            return;

        // Benefit: the hits the pet takes until the tank has the enemy (back).
        float const reclaimSec = TANK_RECLAIM_SEC;
        float const ttd = Value::EnemyTimeToDie(world, *e);
        float const avoided = holding ? Value::HitLoss(world, *e, *petFacts, std::min(reclaimSec, ttd)) :
                                        PullLoss(world, *e, *petFacts, std::min(reclaimSec, ttd));

        // Near aggro, not hit yet: a threat drop on its target (Cower, the pet's facts; user
        // 2026-11-15) that gives back the room the stop rule wants keeps the pet hitting its
        // enemy, for one pet global cooldown instead of a switch or the walk back. Hit already:
        // the brain's rescue (its Cower is a candidate there).
        if (!holding)
        {
            float const needed = NeededHeadroom(world, petGuid, *e) - ThreatView::Headroom(*e, petGuid);
            float const cost = Value::LostAction(world, *petFacts, Value::GCD_SEC);
            for (ThreatDropAbility const& a : petFacts->threatDrops)
            {
                SpellEntry const* spell = sSpellMgr.GetSpellEntry(a.spellId);
                if (!spell || !a.ready || !a.onTarget || a.drop < needed || pet->GetDistance(aim) > a.maxRange ||
                    avoided <= cost || cost > Value::LostAction(world, *petFacts, SWITCH_TARGET_SEC))
                    continue;
                Proposal p = MakeProposal(ProposalTier::Assigned, BotActionType::PetCast, e->guid, spell);
                p.reason = "pet near aggro, cower";
                p.SetValue(avoided, cost);
                out.push_back(p);
                return;
            }
        }

        // A safe enemy, the tank's target first: the pet then fights next to the tank, and an
        // enemy hitting the pet follows it there.
        EnemyFacts const* best = FindSafeTarget(bot, world, plan, petGuid, petFacts->pos, e->guid,
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
}
