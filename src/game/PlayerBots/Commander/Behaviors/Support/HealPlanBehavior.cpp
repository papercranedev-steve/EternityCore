#include "Commander/Behaviors/Support/HealPlanBehavior.h"
#include "Commander/Behaviors/Support/HealPlan.h"
#include "Commander/Behaviors/Support/HotPlan.h"
#include "Commander/Behaviors/Support/ManaBudgetBehavior.h"
#include "Commander/Behaviors/Combat/ThreatBehavior.h"
#include "Commander/Perception/Values/ValueModel.h"
#include "Commander/Perception/Values/ManaWorth.h"
#include "Commander/Perception/Values/SurvivalValue.h"
#include "Commander/Perception/Forecast/ChainHealForecast.h"
#include "Commander/Perception/Forecast/IncomingTimeline.h"
#include "Commander/Perception/WorldModel.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/BotDirectives.h"
#include "Commander/Classes/ClassKit.h"
#include "Commander/CommanderMgr.h"
#include "TeamBotAI.h"
#include "HealSpells.h"
#include "Map.h"
#include "SpellEntry.h"
#include "SpellMgr.h"
#include "Timer.h"
#include <algorithm>

namespace Commander
{
    namespace
    {
        struct Candidate
        {
            Unit* unit = nullptr;
            PartyMemberFacts const* facts = nullptr;
            // Healed after the players (a pet), unless it holds an enemy for the party (an
            // off-tank, doctrine OffTank.h).
            bool last = false;
            HotPlan::HotTarget hotTarget = HotPlan::HotTarget::Member;     // its HoT rank (HotPlan::PickHot)
            HealPlan::Target target;
            HealPlan::Assessment assessment;
            std::vector<HealPlan::Option> options;
            std::vector<SpellEntry const*> spells;      // same index as options
            // Pure HoT ranks the healer may put on (none of its own is up), see HotPlan.h.
            std::vector<HotPlan::HotOption> hots;
            std::vector<SpellEntry const*> hotSpells;   // same index as hots
            HotPlan::Eat eat;

            float Missing() const { return std::max(0.0f, target.maxHealth - target.health); }
        };

        // The eatable HoT on the member (HotPlan::Eat: Swiftmend takes the one with the least
        // time left, anyone's). Empty when the bot has no eat spell or there is none.
        HotPlan::Eat EatOf(TeamBotAI const* bot, Unit* unit, PartyMemberFacts const& m)
        {
            HotPlan::Eat e;
            ClassKit const& kit = bot->Kit();
            SpellEntry const* eat = kit.HotConsumer();
            if (!eat)
                return e;
            PartyHot const* eaten = nullptr;
            uint32 ticks = 0;
            for (PartyHot const& h : m.hots)
            {
                SpellEntry const* spell = sSpellMgr.GetSpellEntry(h.spellId);
                uint32 const n = spell ? kit.HotConsumeTicks(spell) : 0;
                if (!n)
                    continue;
                if (!eaten || h.remainingMs < eaten->remainingMs)
                {
                    eaten = &h;
                    ticks = n;
                }
            }
            if (!eaten)
                return e;

            Player* me = bot->me;
            int32 const cooldownMs = me->GetSpellCooldownLeft(eat);
            e.known = true;
            e.cooldownSec = cooldownMs < 0 ? HealPlan::NO_LIMIT : float(cooldownMs) / 1000.0f;
            e.ready = cooldownMs == 0 && bot->CanTryToCastSpell(unit, eat);
            e.heal = HealSpells::EstimateDirectWithBase(me, unit, eat, eaten->perTick * float(ticks));
            e.ticksLeft = eaten->TicksLeft();
            e.lost = eaten->perTick * float(e.ticksLeft);
            if (e.ticksLeft)
                e.untilLastTickSec = float(eaten->nextTickMs + (e.ticksLeft - 1) * eaten->periodMs) / 1000.0f;
            return e;
        }

        // Swiftmend on the target (HotPlan::Decide said how). A heal order like the heals.
        void ProposeEat(TeamBotAI const* bot, WorldModel const& world, Candidate const& c,
                        HotPlan::EatUse use, float tickSec, std::vector<Proposal>& out)
        {
            bool const emergency = use == HotPlan::EatUse::Emergency;
            float const effective = std::min(c.eat.heal, c.Missing());
            Proposal p;
            p.kind = ProposalKind::HealOrder;
            p.tier = emergency ? ProposalTier::Urgent : ProposalTier::Assigned;
            p.source = "heal";
            p.reason = emergency ? "emergency: eat HoT, instant heal" :
                       use == HotPlan::EatUse::Free ? "HoT's last tick: eat it, nearly free" :
                                                      "lift: eat HoT instead of a cast";
            p.action.type = BotActionType::Cast;
            p.action.target = c.facts->guid;
            p.action.spell = bot->Kit().HotConsumer();
            float benefit = Value::Points(world, effective);
            if (emergency && c.assessment.timeToDie < HealPlan::GCD_SEC + tickSec * 2.0f)
                benefit += Value::DeathPenalty(*c.facts);
            // Cost: the overheal and the ticks the HoT still had.
            p.SetValue(benefit, Value::Points(world, c.eat.heal - effective + c.eat.lost));
            out.push_back(p);
        }

        // The healer's HoT on a member being hit (HotPlan::PickHot). False = none fits. A pet
        // that holds no enemy for the party gets none while mana is scarce (HealPlan::ManaWeight
        // at PET_HOT_MAX_MANA_WEIGHT or more): its health matters least.
        bool ProposeHot(WorldModel const& world, Candidate const& c, float manaWeight, std::vector<Proposal>& out)
        {
            if (c.hots.empty() || c.target.intake <= 0.0f)
                return false;
            if (c.hotTarget == HotPlan::HotTarget::Pet && manaWeight >= HealPlanBehavior::PET_HOT_MAX_MANA_WEIGHT)
                return false;
            int const index = HotPlan::PickHot(c.hots, c.hotTarget);
            if (index < 0)
                return false;
            HotPlan::HotOption const& o = c.hots[index];
            float const effective = std::min(o.total, o.need);
            Proposal p;
            p.kind = ProposalKind::HealOrder;
            p.tier = ProposalTier::Assigned;
            p.source = "heal";
            p.reason = "being hit, no own HoT: HoT";
            p.action.type = BotActionType::Cast;
            p.action.target = c.facts->guid;
            p.action.spell = c.hotSpells[index];
            p.SetValue(Value::Points(world, effective), Value::Points(world, o.total - effective));
            out.push_back(p);
            return true;
        }

        bool IsMoreUrgent(Candidate const& a, Candidate const& b)
        {
            bool const ea = a.assessment.need == HealPlan::Need::Emergency;
            bool const eb = b.assessment.need == HealPlan::Need::Emergency;
            if (ea != eb)
                return ea;
            if (ea)
                return a.assessment.timeToDie < b.assessment.timeToDie;
            if (a.last != b.last)
                return !a.last;
            // A chain and a lift by who reaches the reserve first: until 2026-11-02 a chain came
            // before every lift, and the priest topped up a rogue at 65% while the tank sank from
            // 53% into an emergency (strat-20261102-012937). The chain only breaks ties.
            if (a.assessment.timeToReserve != b.assessment.timeToReserve)
                return a.assessment.timeToReserve < b.assessment.timeToReserve;
            bool const ca = a.assessment.need == HealPlan::Need::Chain;
            bool const cb = b.assessment.need == HealPlan::Need::Chain;
            if (ca != cb)
                return ca;
            return a.facts->healthPct < b.facts->healthPct;
        }

        // Power Word: Shield instead of the picked heal (see the header): an emergency the heal
        // does not reach in time, or the healer itself being hit below SHIELD_SELF_PCT before a
        // heal with a cast time (no pushback under the shield). A heal order like the heals.
        bool ProposeShield(TeamBotAI const* bot, WorldModel const& world,
                           Unit* unit, PartyMemberFacts const& facts, HealPlan::Target const& target,
                           HealPlan::Assessment const& assessment, HealPlan::Option const* heal,
                           std::vector<Proposal>& out)
        {
            SpellEntry const* shield = bot->Kit().AllyShield();
            if (!shield)
                return false;

            // Someone about to die: the team rescue's choice (RescueAssigner, Takeover::Survival).
            bool const emergency = assessment.need == HealPlan::Need::Emergency;
            bool const self = unit == bot->me;
            bool const pushback = self && target.intake > 0.0f && heal && heal->castSec > 0.0f &&
                                  unit->GetHealthPercent() < HealPlanBehavior::SHIELD_SELF_PCT;
            if (!pushback)
                return false;
            // Weakened Soul gives shield immunity; one shield at a time.
            if (unit->IsImmuneToMechanic(MECHANIC_SHIELD) || unit->HasAura(shield->Id) ||
                !bot->CanTryToCastSpell(unit, shield))
                return false;

            float const absorb = float(std::max(0, shield->CalculateSimpleValue(EFFECT_INDEX_0)));
            float const durationSec = std::max(0, shield->GetDuration()) / 1000.0f;
            float const taken = std::min(absorb, target.intake * durationSec);
            // Benefit: the hits it takes.
            float const benefit = Value::Points(world, taken);

            Proposal p;
            p.kind = ProposalKind::HealOrder;
            p.tier = emergency ? ProposalTier::Urgent : ProposalTier::Assigned;
            p.source = "heal";
            p.reason = "hit while casting: shield";
            p.action.type = BotActionType::Cast;
            p.action.target = facts.guid;
            p.action.spell = shield;
            // Cost: absorb left over when it runs out (like overheal).
            p.SetValue(benefit, Value::Points(world, absorb - taken));
            out.push_back(p);
            return true;
        }

        // The free self heal (Desperate Prayer, see the header): hit, and missing at least about
        // its amount. Not while another member needs an emergency heal (it takes a GCD).
        bool ProposeFreeSelfHeal(TeamBotAI const* bot, WorldModel const& world, PartyMemberFacts const& self,
                                 bool otherEmergency, std::vector<Proposal>& out)
        {
            SpellEntry const* spell = bot->Kit().FreeSelfHeal();
            if (!spell || otherEmergency || !self.CanAct())
                return false;
            Player* me = bot->me;
            if (Value::TakenDps(world, self.guid) <= 0.0f)
                return false;
            // (from Shadowform: the arbiter leaves the form first, TeamBotAI::DoCastWithStance)
            if (!me->IsSpellReady(spell) || !bot->CanCastWithStance(me, spell))
                return false;
            float const heal = HealSpells::Estimate(me, me, spell).direct;
            float const missing = float(me->GetMaxHealth()) - float(me->GetHealth());
            if (heal <= 0.0f || missing < heal * (1.0f - HealPlan::OVERHEAL_MAX))
                return false;

            Proposal p;
            p.tier = SurvivalValue::Of(world, self).InDanger() ? ProposalTier::Urgent : ProposalTier::Assigned;
            p.source = "heal";
            p.reason = "hit, free self heal fits";
            p.action.type = BotActionType::Cast;
            p.action.target = self.guid;
            p.action.spell = spell;
            float const effective = std::min(heal, missing);
            p.SetValue(Value::Points(world, effective), Value::Points(world, heal - effective));
            out.push_back(p);
            return true;
        }

        // A heal boost that makes the next heal bigger or free is ready (Inner Focus, Divine
        // Favor; user 2026-11-15: Divine Favor as Inner Focus): the deficit the lift waits for,
        // so the boosted biggest heal (crit counted) fills it (HealPlan::Target::boostDeficit).
        // Not Nature's Swiftness (instant: emergencies only). 0 = none ready.
        float BoostWait(TeamBotAI const* bot, std::vector<HealPlan::Option> const& options)
        {
            Player* me = bot->me;
            for (HealBoostSpell const& s : bot->Kit().HealBoosts())
            {
                if (!s.spell || s.instant)
                    continue;
                // Ready for the coming heal: the global cooldown of the heal before does not
                // count (it ended the wait in every tick between two heals).
                if (!me->HasAura(s.spell->Id) &&
                    (!me->IsSpellReady(s.spell) ||
                     !bot->CanTryToCastSpellBesidesTimers(me, s.spell, me->GetShapeshiftForm(),
                                                          me->GetPower(Powers(s.spell->powerType)))))
                    continue;
                HealPlan::Boost b;
                b.free = s.free;
                b.critChance = s.critChance;
                // The heals PickBoosted takes outside an emergency.
                float biggest = 0.0f;
                for (HealPlan::Option const& o : options)
                    if (!o.emergencyOnly)
                        biggest = std::max(biggest, HealPlan::BoostedHeal(o, b));
                return biggest * (1.0f - HealPlan::OVERHEAL_MAX);
            }
            return 0.0f;
        }

        // A ready heal boost that suits this heal (HealPlan::PickBoosted): the option to cast
        // with it, and the boost; -1 = none.
        int PickBoost(TeamBotAI const* bot, Candidate const& c, int picked,
                      SpellEntry const*& boostSpell, HealPlan::Boost& boost)
        {
            Player* me = bot->me;
            for (HealBoostSpell const& s : bot->Kit().HealBoosts())
            {
                if (!s.spell)
                    continue;
                // Already on (put up in a tick whose heal did not go out): counts as ready.
                if (!me->HasAura(s.spell->Id) && (!me->IsSpellReady(s.spell) || !bot->CanTryToCastSpell(me, s.spell)))
                    continue;
                HealPlan::Boost b;
                b.free = s.free;
                b.critChance = s.critChance;
                b.instant = s.instant;
                int const index = HealPlan::PickBoosted(c.target, c.assessment, c.options, picked, b);
                if (index < 0)
                    continue;
                boostSpell = s.spell;
                boost = b;
                return index;
            }
            return -1;
        }

        char const* ReasonOf(HealPlan::Need need)
        {
            switch (need)
            {
                case HealPlan::Need::Emergency: return "emergency heal";
                case HealPlan::Need::Chain:     return "continue heal chain";
                default:                        return "deficit reached, lift";
            }
        }
    }

    bool HealPlanBehavior::AppliesTo(TeamBotAI const* bot) const
    {
        // (heals the team: a healer, or a bot given the heal function because nobody else heals)
        return bot->GetRole() == ROLE_HEALER || bot->HealsForTeam() || bot->Kit().FreeSelfHeal();
    }

    bool HealPlanBehavior::ControlsSelfHealOf(TeamBotAI const* bot, WorldModel const& world)
    {
        return sCommanderMgr.IsEnabled() && sCommanderMgr.IsModuleEnabled("heal") &&
               world.HasPartyEnemy();
    }

    bool HealPlanBehavior::TakesOver(Takeover what, TeamBotAI const* bot, WorldModel const& world,
                                     TeamPlan const& /*plan*/) const
    {
        return what == Takeover::Healing && ControlsHealingOf(bot, world);
    }

    bool HealPlanBehavior::ControlsHealingOf(TeamBotAI const* bot, WorldModel const& world)
    {
        if (!sCommanderMgr.IsEnabled() || !sCommanderMgr.IsModuleEnabled("heal") || !world.HasPartyEnemy())
            return false;
        // Heals the team by function (PartyMemberFacts::HealsTeam: a healer, or nobody else heals).
        PartyMemberFacts const* self = world.FindMember(bot->me->GetObjectGuid());
        return self ? self->HealsTeam() : bot->GetRole() == ROLE_HEALER;
    }

    void HealPlanBehavior::Propose(TeamBotAI const* bot, WorldModel const& world,
                                   TeamPlan const& plan, std::vector<Proposal>& out) const
    {
        Player* me = bot->me;
        PartyMemberFacts const* myFacts = world.FindMember(me->GetObjectGuid());
        if (!myFacts)
            return;
        if (!ControlsHealingOf(bot, world))
        {
            // A bot that is not a healer: only its free self heal (user 2026-11-14).
            if (ControlsSelfHealOf(bot, world))
                ProposeFreeSelfHeal(bot, world, *myFacts, false, out);
            return;
        }

        // Still out of combat and held by the pull's opening (PullOpening.h: healing starts last,
        // its threat lands on every enemy at once): emergencies only.
        bool const heldByPull = !me->IsInCombat() && plan.pull.HoldsWaiting(true);
        SpellEntry const* freeSelfHeal = bot->Kit().FreeSelfHeal();

        uint32 const now = WorldTimer::getMSTime();
        uint32 const tickMs = sCommanderMgr.GetBotUpdateInterval();

        HealPlan::Healer healer;
        healer.regen = myFacts->manaRegen;
        healer.regenCasting = myFacts->manaRegenCasting;
        healer.tickSec = tickMs / 1000.0f;
        ManaBudgetBehavior::Budget budget;
        float const tension = ManaBudgetBehavior::ComputeBudget(bot, world, budget) ? budget.tension : 0.0f;
        float const manaPct = myFacts->maxMana ? float(myFacts->mana) * 100.0f / float(myFacts->maxMana) : 100.0f;
        healer.manaWeight = HealPlan::ManaWeight(tension, manaPct);
        healer.steady = bot->Kit().HealsSteadily();

        BotDirectives const& directives = bot->Directives();
        ClassKit const& kit = bot->Kit();
        std::vector<Candidate> all;
        for (PartyMemberFacts const& m : world.party)
        {
            if (!m.alive || m.charmed)
                continue;
            Unit* unit = me->GetMap()->GetUnit(m.guid);
            // A steady healer heals before the hits: full targets too (HealPlan.h).
            if (!unit || !bot->IsValidHealTarget(unit, healer.steady ? 101.0f : 100.0f))
                continue;

            // Castable single-target heals; a HoT of the healer's own that is up is left out
            // (CombatBotBaseAI::ChooseHeal: not refreshed early, not put on twice).
            HealChoice choice;
            bot->ChooseHeal(unit, choice, false);
            if (choice.options.empty())
                continue;

            Candidate c;
            c.unit = unit;
            c.facts = &m;
            bool const offTank = !plan.doctrine.OffTankEnemyOf(m.guid).IsEmpty();
            c.last = m.isPet && !offTank;
            c.hotTarget = c.last ? HotPlan::HotTarget::Pet :
                          (offTank || m.role == ROLE_TANK) ? HotPlan::HotTarget::Tank : HotPlan::HotTarget::Member;
            c.target.health = float(unit->GetHealth());
            c.target.maxHealth = float(unit->GetMaxHealth());
            // Damage the HoTs already on it do not cover.
            float hotPerSec = 0.0f;
            for (PartyHot const& h : m.hots)
                hotPerSec += h.PerSec();
            c.target.intake = std::max(0.0f, Value::TakenDps(world, m.guid) - hotPerSec);
            c.target.burst = m.takenBurst;
            // The hits coming one by one, the HoT ticks on it between them (IncomingTimeline.h).
            {
                IncomingTimeline::Timeline timeline = IncomingTimeline::Of(world, m);
                timeline.AddHots(m);
                float const health = c.target.health + m.absorb;
                c.target.timeToDie = timeline.TimeToLose(health, false);
                c.target.worstTimeToDie = timeline.TimeToLose(health, true);
                c.target.timeToReserve = timeline.TimeToLose(std::max(0.0f, health - HealPlan::ReserveHealth(c.target)), false);
            }
            c.target.chained = directives.IsHealChain(m.guid, now, tickMs + CHAIN_GAP_MS);
            for (HealSpells::Option const& o : choice.options)
            {
                // The free self heal has its own rule (ProposeFreeSelfHeal).
                HealUse const use = kit.CombatHealUse(o.spell);
                if (o.spell == freeSelfHeal || use == HealUse::Never)
                    continue;
                if (o.amount.direct <= 0.0f)
                {
                    // A pure HoT (ProposeHot).
                    if (use != HealUse::Normal || o.amount.periodic <= 0.0f)
                        continue;
                    HotPlan::HotOption h;
                    h.total = o.amount.periodic;
                    h.need = c.Missing() + c.target.intake * o.amount.periodicSec * HotPlan::MATCH_SHARE;
                    h.mana = o.mana;
                    c.hots.push_back(h);
                    c.hotSpells.push_back(o.spell);
                    continue;
                }
                HealPlan::Option p;
                p.heal = o.amount.direct;
                if (o.amount.periodic > 0.0f)
                    p.heal += std::min(o.amount.periodic,
                                       c.target.intake * o.amount.periodicSec * HealSpells::HOT_INCOMING_SHARE);
                p.mana = o.mana;
                p.castSec = o.castSec;
                p.emergencyOnly = use == HealUse::EmergencyOnly;
                // Chain Heal: what its jumps heal on the others (2026-11-15).
                ChainHealForecast::Jumps const jumps = ChainHealForecast::Of(bot, world, m, o.spell, o.castSec);
                p.otherHeal = jumps.heal;
                p.otherEffective = jumps.effective;
                c.options.push_back(p);
                c.spells.push_back(o.spell);
            }
            c.eat = EatOf(bot, unit, m);
            c.target.waitDeficit = HotPlan::WaitDeficit(c.eat);
            c.target.boostDeficit = BoostWait(bot, c.options);
            c.assessment = HealPlan::Assess(c.target, healer, c.options);
            all.push_back(std::move(c));
        }

        std::vector<Candidate*> candidates;
        for (Candidate& c : all)
        {
            if (c.assessment.need != HealPlan::Need::None && c.assessment.need != HealPlan::Need::Rest)
                candidates.push_back(&c);
            // A ready heal boost holds this target (its gap grows for one boosted big heal):
            // written to the decision log as a dropped order, nothing is cast.
            else if (c.assessment.boostHeld && c.Missing() > 0.0f && !kit.HealBoosts().empty())
            {
                Proposal p;
                p.kind = ProposalKind::HealOrder;
                p.tier = ProposalTier::Assigned;
                p.source = "heal";
                p.reason = "heal boost ready: held until one boosted big heal fills the gap";
                p.action.type = BotActionType::Cast;
                p.action.target = c.facts->guid;
                p.action.spell = kit.HealBoosts().front().spell;
                p.SetValue(0.0f, 0.0f);
                out.push_back(p);
            }
        }
        std::stable_sort(candidates.begin(), candidates.end(),
                         [](Candidate const* a, Candidate const* b) { return IsMoreUrgent(*a, *b); });

        // The free self heal first, unless someone else needs an emergency heal (user 2026-11-14).
        bool const otherEmergency = std::any_of(candidates.begin(), candidates.end(), [me](Candidate const* c)
            { return c->unit != me && c->assessment.need == HealPlan::Need::Emergency; });
        if (!candidates.empty() && !heldByPull && ProposeFreeSelfHeal(bot, world, *myFacts, otherEmergency, out))
            return;

        bool const checkAggro = sCommanderMgr.IsModuleEnabled("threat");
        for (Candidate* pc : candidates)
        {
            Candidate& c = *pc;
            bool const emergency = c.assessment.need == HealPlan::Need::Emergency;
            if (heldByPull && !emergency)
                continue;

            // Swiftmend first (instant; in an emergency before Nature's Swiftness), then the
            // healer's HoT on a lift target, then the heal.
            HotPlan::EatUse const eatUse = HotPlan::Decide(c.eat, c.Missing(), c.assessment.need);
            if (eatUse != HotPlan::EatUse::None)
            {
                ProposeEat(bot, world, c, eatUse, healer.tickSec, out);
                return;
            }
            if (!emergency && ProposeHot(world, c, healer.manaWeight, out))
                return;
            // Emergencies prefer heals that do not pull, but take one that does (HealPlan::Pick).
            if (checkAggro && c.unit->GetHealthPercent() >= ThreatBehavior::HEAL_IGNORE_THREAT_PCT)
                for (size_t i = 0; i < c.options.size(); ++i)
                    c.options[i].pulls = ThreatBehavior::HealPullLoss(bot, world, c.unit, c.spells[i]) > 0.0f;

            int index = HealPlan::Pick(c.target, healer, c.assessment, c.options);
            if (ProposeShield(bot, world, c.unit, *c.facts, c.target, c.assessment,
                              index >= 0 ? &c.options[index] : nullptr, out))
                return;

            // A ready heal boost that suits this heal: cast with the heal it waits for.
            SpellEntry const* boostSpell = nullptr;
            HealPlan::Boost boost;
            int const boosted = PickBoost(bot, c, index, boostSpell, boost);
            if (boosted >= 0)
                index = boosted;
            if (index < 0)
                continue;

            HealPlan::Option const& o = c.options[index];
            float heal = o.heal;
            float effective = o.effective;
            float castSec = o.castSec;
            if (boostSpell)
            {
                castSec = boost.instant ? 0.0f : o.castSec;
                heal = HealPlan::BoostedHeal(o, boost);
                float const missing = std::max(0.0f, c.target.maxHealth - c.target.health);
                effective = std::min(heal, std::min(c.target.maxHealth, missing + c.target.intake * castSec));
            }
            Proposal p;
            p.kind = ProposalKind::HealOrder;
            p.tier = emergency ? ProposalTier::Urgent : ProposalTier::Assigned;
            p.source = "heal";
            p.reason = boostSpell ? (boost.instant ? "emergency: boost, instant big heal" : "big deficit: boost, big heal")
                                  : ReasonOf(c.assessment.need);
            p.action.type = BotActionType::Cast;
            p.action.target = c.facts->guid;
            p.action.spell = c.spells[index];
            p.action.prepSpell = boostSpell;
            // Benefit: the health it restores, plus the death it prevents when the target would
            // not last until the next chance to heal it, plus the mana a free boost saves.
            // Cost: the overheal. A chained heal's jumps count on both sides (Option::otherHeal).
            heal += o.otherHeal;
            effective += o.otherEffective;
            float benefit = Value::Points(world, effective);
            if (emergency && c.assessment.timeToDie < std::max(HealPlan::GCD_SEC, castSec) + healer.tickSec * 2.0f)
                benefit += Value::DeathPenalty(*c.facts);
            if (boostSpell && boost.free)
                benefit += ManaWorth::Points(world, *myFacts, o.mana);
            p.SetValue(benefit, Value::Points(world, heal - effective));
            out.push_back(p);
            return;
        }

        // No lift this tick: a free Swiftmend or a HoT on whoever takes the most damage.
        if (heldByPull)
            return;
        std::vector<Candidate*> others;
        for (Candidate& c : all)
            others.push_back(&c);
        std::stable_sort(others.begin(), others.end(),
                         [](Candidate const* a, Candidate const* b)
                         {
                             // Pets after the players (Candidate::last), then most damage taken.
                             if (a->last != b->last)
                                 return !a->last;
                             return a->target.intake > b->target.intake;
                         });
        for (Candidate* pc : others)
        {
            HotPlan::EatUse const eatUse = HotPlan::Decide(pc->eat, pc->Missing(), pc->assessment.need);
            if (eatUse != HotPlan::EatUse::None)
            {
                ProposeEat(bot, world, *pc, eatUse, healer.tickSec, out);
                return;
            }
            if (ProposeHot(world, *pc, healer.manaWeight, out))
                return;
        }
    }
}
