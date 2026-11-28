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
#include "Commander/Perception/Forecast/PartyHealForecast.h"
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
#include <cmath>

namespace Commander
{
    namespace
    {
        struct Candidate
        {
            Unit* unit = nullptr;
            // A member, or an ally the party protects (AllyFacts::protect, boss doc G3).
            ObjectGuid guid;
            float healthPct = 0.0f;
            float deathPenalty = 0.0f;      // Value::DeathPenalty of the member or ally
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
            // A member whose gap does not grow by surprise: no tank, no pet, no enemy on it
            // (HealUse::SettledOnly heals; party heal doc).
            bool settled = false;
            // Its health now; target.health of a settled member also counts the HoT ticks still
            // to come that its damage does not eat (header), so no heal goes on a gap a HoT fills.
            float rawHealth = 0.0f;

            float Missing() const { return std::max(0.0f, target.maxHealth - target.health); }
            float RawMissing() const { return std::max(0.0f, target.maxHealth - rawHealth); }
        };

        // An enemy that is not controlled has `guid` as its victim.
        bool EnemyOn(WorldModel const& world, ObjectGuid guid)
        {
            return std::any_of(world.enemies.begin(), world.enemies.end(), [guid](EnemyFacts const& e)
                { return !e.crowdControlled && e.victim == guid; });
        }

        // HoT healing still to come on `m` beyond the damage over the HoTs' time: what they fill
        // of its gap.
        float HotSurplus(WorldModel const& world, PartyMemberFacts const& m)
        {
            float left = 0.0f;
            uint32 longestMs = 0;
            for (PartyHot const& h : m.hots)
            {
                left += h.perTick * float(h.TicksLeft());
                longestMs = std::max(longestMs, h.remainingMs);
            }
            if (left <= 0.0f)
                return 0.0f;
            return std::max(0.0f, left - Value::TakenDps(world, m.guid) * float(longestMs) / 1000.0f);
        }

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
            float const effective = std::min(c.eat.heal, c.RawMissing());
            Proposal p;
            p.kind = ProposalKind::HealOrder;
            p.tier = emergency ? ProposalTier::Urgent : ProposalTier::Assigned;
            p.source = "heal";
            p.reason = emergency ? "emergency: eat HoT, instant heal" :
                       use == HotPlan::EatUse::Free ? "HoT's last tick: eat it, nearly free" :
                                                      "lift: eat HoT instead of a cast";
            p.action.type = BotActionType::Cast;
            p.action.target = c.guid;
            p.action.spell = bot->Kit().HotConsumer();
            float benefit = Value::Points(world, effective);
            if (emergency && c.assessment.timeToDie < HealPlan::GCD_SEC + tickSec * 2.0f)
                benefit += c.deathPenalty;
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
            p.action.target = c.guid;
            p.action.spell = c.hotSpells[index];
            p.SetValue(Value::Points(world, effective), Value::Points(world, o.total - effective));
            out.push_back(p);
            return true;
        }

        // The least CastCost per health of a single-target heal on `c` now: options that heal
        // (no emergency-only ones) and overheal at most OVERHEAL_MAX when they land.
        // HealPlan::NO_LIMIT = none.
        float BestSingleRate(HealPlan::Healer const& healer, Candidate const& c)
        {
            float best = HealPlan::NO_LIMIT;
            for (HealPlan::Option const& o : c.options)
            {
                if (o.heal <= 0.0f || o.emergencyOnly)
                    continue;
                float const effective = HealPlan::EffectiveAtLanding(c.target, o);
                if (effective <= 0.0f || o.heal - effective > HealPlan::OVERHEAL_MAX * o.heal)
                    continue;
                best = std::min(best, HealPlan::CastCost(healer, o) / effective);
            }
            return best;
        }

        // One castable rank of a party heal and what it does (PartyHealForecast).
        struct PartyRank
        {
            SpellEntry const* spell = nullptr;
            HealPlan::Option cast;      // mana and cast time only
            PartyHealForecast::Result r;
        };

        // `r` with every heal scaled by `factor` (a boost's crit), capped again at the missing
        // health.
        PartyHealForecast::Result Scaled(PartyHealForecast::Result const& r, float factor)
        {
            PartyHealForecast::Result s = r;
            s.heal = s.effective = s.hurtHeal = 0.0f;
            for (PartyHealForecast::Reach& t : s.targets)
            {
                t.heal *= factor;
                t.effective = std::min(t.heal, t.missing);
                s.heal += t.heal;
                s.effective += t.effective;
                if (t.missing > 0.0f)
                    s.hurtHeal += t.heal;
            }
            return s;
        }

        // The party heal's own rules (header "Party heal"): hurt targets, overheal on them.
        bool PartyQualifies(PartyHealForecast::Result const& r)
        {
            return r.hurt >= HealPlanBehavior::PARTY_MIN_HURT && r.effective > 0.0f &&
                   r.hurtHeal - r.effective <= HealPlan::OVERHEAL_MAX * r.hurtHeal;
        }

        // A non-instant heal boost castable now (Inner Focus), as PickBoost checks it.
        bool ReadyBoost(TeamBotAI const* bot, SpellEntry const*& spell, HealPlan::Boost& boost)
        {
            Player* me = bot->me;
            for (HealBoostSpell const& s : bot->Kit().HealBoosts())
            {
                if (!s.spell || s.instant)
                    continue;
                if (!me->HasAura(s.spell->Id) && (!me->IsSpellReady(s.spell) || !bot->CanTryToCastSpell(me, s.spell)))
                    continue;
                spell = s.spell;
                boost.free = s.free;
                boost.critChance = s.critChance;
                return true;
            }
            return false;
        }

        // The mana a single-target boost would save: the biggest heal on a hurt member (the one
        // HealPlan::PickBoosted casts with it).
        float SingleBoostMana(std::vector<Candidate> const& all)
        {
            float mana = 0.0f;
            for (Candidate const& c : all)
            {
                if (c.Missing() <= 0.0f)
                    continue;
                HealPlan::Option const* big = nullptr;
                for (HealPlan::Option const& o : c.options)
                    if (!o.emergencyOnly && o.hotSec <= 0.0f && o.heal > 0.0f && (!big || o.heal > big->heal))
                        big = &o;
                if (big)
                    mana = std::max(mana, big->mana);
            }
            return mana;
        }

        // A ready boost waits for this rank boosted (header "Party heal", Inner Focus): at least
        // PARTY_MIN_HURT of its targets miss the boosted heal x (1 - OVERHEAL_MAX) within
        // PARTY_BOOST_WAIT_SEC at the damage they take.
        bool BoostGapComing(WorldModel const& world, PartyHealForecast::Result const& boosted)
        {
            uint32 soon = 0;
            for (PartyHealForecast::Reach const& t : boosted.targets)
            {
                float const need = t.heal * (1.0f - HealPlan::OVERHEAL_MAX) - t.missing;
                if (need <= 0.0f)
                {
                    ++soon;
                    continue;
                }
                float const dps = Value::TakenDps(world, t.guid);
                if (dps > 0.0f && need / dps <= HealPlanBehavior::PARTY_BOOST_WAIT_SEC)
                    ++soon;
            }
            return soon >= HealPlanBehavior::PARTY_MIN_HURT;
        }

        // A party heal around the healer (Prayer of Healing, every rank; header "Party heal").
        bool ProposePartyHeal(TeamBotAI const* bot, WorldModel const& world, HealPlan::Healer const& healer,
                              std::vector<Candidate> const& all, bool checkAggro, std::vector<Proposal>& out)
        {
            Player* me = bot->me;
            ClassKit const& kit = bot->Kit();
            for (Candidate const& c : all)
                if (c.assessment.need == HealPlan::Need::Emergency)
                    return false;   // the dying one first, by its own heal

            // The castable ranks; one that someone it reaches would not last for is out (that
            // one's own heal first).
            std::vector<PartyRank> ranks;
            for (SpellEntry const* spell : bot->m_spellListDirectHeal)
            {
                if (!PartyHealForecast::IsPartyHeal(spell) || kit.CombatHealUse(spell) == HealUse::Never ||
                    !bot->CanTryToCastHeal(me, spell))
                    continue;
                PartyRank k;
                k.spell = spell;
                k.cast.castSec = float(spell->GetCastTime(me)) / 1000.0f;
                k.cast.mana = HealSpells::ManaCost(me, spell);
                k.r = PartyHealForecast::Of(bot, world, spell, k.cast.castSec);
                if (k.r.targets.empty())
                    continue;
                bool late = false;
                for (PartyHealForecast::Reach const& t : k.r.targets)
                    for (Candidate const& c : all)
                        if (c.guid == t.guid && t.missing > 0.0f &&
                            c.assessment.timeToReserve < k.cast.castSec + HealPlan::LANDING_MARGIN_SEC)
                            late = true;
                if (!late)
                    ranks.push_back(k);
            }
            if (ranks.empty())
                return false;

            // Inner Focus (user 2026-11-28): on the party heal when that saves more mana than the
            // single heal it would otherwise wait for, and the boosted heal fills its targets.
            SpellEntry const* boostSpell = nullptr;
            HealPlan::Boost boost;
            if (ReadyBoost(bot, boostSpell, boost))
            {
                float const factor = 1.0f + std::min(1.0f, std::max(0.0f, boost.critChance)) * (HealPlan::CRIT_MULT - 1.0f);
                float const singleMana = SingleBoostMana(all);
                PartyRank const* pick = nullptr;
                PartyHealForecast::Result pickR;
                PartyRank const* waitFor = nullptr;     // the biggest rank worth the boost
                for (PartyRank const& k : ranks)
                {
                    if (k.cast.mana < singleMana)
                        continue;
                    PartyHealForecast::Result const s = Scaled(k.r, factor);
                    if (!waitFor || k.cast.mana > waitFor->cast.mana)
                        waitFor = &k;
                    if (!PartyQualifies(s) || (pick && s.effective <= pickR.effective))
                        continue;
                    if (checkAggro && ThreatBehavior::HealPullLossOf(bot, world, me, k.spell, s.effective) > 0.0f)
                        continue;
                    pick = &k;
                    pickR = s;
                }
                PartyMemberFacts const* self = world.FindMember(me->GetObjectGuid());
                if (pick && self)
                {
                    Proposal p;
                    p.kind = ProposalKind::HealOrder;
                    p.tier = ProposalTier::Assigned;
                    p.source = "heal";
                    p.reason = "party hurt: boost, big party heal";
                    p.action.type = BotActionType::Cast;
                    p.action.target = me->GetObjectGuid();
                    p.action.spell = pick->spell;
                    p.action.prepSpell = boostSpell;
                    float benefit = Value::Points(world, pickR.effective);
                    if (boost.free)
                        benefit += ManaWorth::Points(world, *self, pick->cast.mana);
                    p.SetValue(benefit, Value::Points(world, pickR.heal - pickR.effective));
                    out.push_back(p);
                    return true;
                }
                // The big gap is coming: no small party heal fills it first (the single heals
                // still go by their own rules). Written to the decision log as a dropped order.
                if (waitFor && BoostGapComing(world, Scaled(waitFor->r, factor)))
                {
                    Proposal p;
                    p.kind = ProposalKind::HealOrder;
                    p.tier = ProposalTier::Assigned;
                    p.source = "heal";
                    p.reason = "heal boost ready: party heal held until the boosted big one fills";
                    p.action.type = BotActionType::Cast;
                    p.action.target = me->GetObjectGuid();
                    p.action.spell = waitFor->spell;
                    p.SetValue(0.0f, 0.0f);
                    out.push_back(p);
                    return false;
                }
            }

            PartyRank const* bestRank = nullptr;
            float bestRate = 0.0f;
            for (PartyRank const& k : ranks)
            {
                PartyHealForecast::Result const& r = k.r;
                if (!PartyQualifies(r))
                    continue;
                float const rate = HealPlan::CastCost(healer, k.cast) / r.effective;
                if (bestRank && (rate > bestRate || (std::fabs(rate - bestRate) <= HealPlan::TIE_FRACTION * bestRate &&
                                                     r.effective <= bestRank->r.effective)))
                    continue;
                if (checkAggro && ThreatBehavior::HealPullLossOf(bot, world, me, k.spell, r.effective) > 0.0f)
                    continue;
                bestRank = &k;
                bestRate = rate;
            }
            if (!bestRank)
                return false;
            SpellEntry const* bestSpell = bestRank->spell;
            PartyHealForecast::Result const& best = bestRank->r;

            // Cheaper per health than any single heal on the members it reaches.
            float single = HealPlan::NO_LIMIT;
            for (PartyHealForecast::Reach const& t : best.targets)
                for (Candidate const& c : all)
                    if (c.guid == t.guid)
                        single = std::min(single, BestSingleRate(healer, c));
            if (single < HealPlan::NO_LIMIT && bestRate >= single * (1.0f - HealPlan::TIE_FRACTION))
                return false;

            Proposal p;
            p.kind = ProposalKind::HealOrder;
            p.tier = ProposalTier::Assigned;
            p.source = "heal";
            p.reason = "party hurt: party heal";
            p.action.type = BotActionType::Cast;
            p.action.target = me->GetObjectGuid();
            p.action.spell = bestSpell;
            p.SetValue(Value::Points(world, best.effective), Value::Points(world, best.heal - best.effective));
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
            return a.healthPct < b.healthPct;
        }

        // Power Word: Shield instead of the picked heal (see the header): an emergency the heal
        // does not reach in time, or the healer itself being hit below SHIELD_SELF_PCT before a
        // heal with a cast time (no pushback under the shield). A heal order like the heals.
        bool ProposeShield(TeamBotAI const* bot, WorldModel const& world,
                           Unit* unit, ObjectGuid targetGuid, HealPlan::Target const& target,
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
            p.action.target = targetGuid;
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
                    if (!o.emergencyOnly && o.hotSec <= 0.0f)   // never boosted (HealPlan::PickBoosted)
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
        healer.fiveSecondLeftSec = float(myFacts->fiveSecondRuleMs) / 1000.0f;
        // The healer's own coming lockout (an area stun on the party): the last heal before it
        // must carry its targets through it (HealPlan::Healer::NextChanceSec).
        if (IncomingTimeline::Lockout const* lockout = IncomingTimeline::Of(world, *myFacts).NextLockout())
        {
            healer.lockoutStartSec = lockout->startSec;
            healer.lockoutEndSec = lockout->endSec;
        }

        BotDirectives const& directives = bot->Directives();
        ClassKit const& kit = bot->Kit();
        std::vector<Candidate> all;
        // Castable heals on `c` (ChooseHeal's options): direct heals into options, pure HoTs into
        // hots. `m`: the member (Chain Heal's jumps), null for an ally.
        auto const addOptions = [&](Candidate& c, HealChoice const& choice, PartyMemberFacts const* m)
        {
            for (HealSpells::Option const& o : choice.options)
            {
                // The free self heal has its own rule (ProposeFreeSelfHeal).
                HealUse const use = kit.CombatHealUse(o.spell);
                if (o.spell == freeSelfHeal || use == HealUse::Never)
                    continue;
                if (use == HealUse::SettledOnly)
                {
                    // A pure HoT on a settled gap: valued with the direct heals (HealPlan.h).
                    if (!c.settled || o.amount.direct > 0.0f || o.amount.periodic <= 0.0f || o.amount.periodicSec <= 0.0f)
                        continue;
                    HealPlan::Option p;
                    p.heal = o.amount.periodic;
                    p.mana = o.mana;
                    p.castSec = o.castSec;
                    p.hotSec = o.amount.periodicSec;
                    c.options.push_back(p);
                    c.spells.push_back(o.spell);
                    continue;
                }
                if (o.amount.direct <= 0.0f)
                {
                    // A pure HoT (ProposeHot).
                    if (use != HealUse::Normal || o.amount.periodic <= 0.0f)
                        continue;
                    HotPlan::HotOption h;
                    h.total = o.amount.periodic;
                    h.need = c.Missing() + c.target.intake * o.amount.periodicSec * HotPlan::MATCH_SHARE;
                    // Its mana with the regeneration the cast takes away (HealPlan::CastCost).
                    HealPlan::Option cast;
                    cast.mana = o.mana;
                    cast.castSec = o.castSec;
                    h.mana = HealPlan::CastCost(healer, cast);
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
                if (m)
                {
                    ChainHealForecast::Jumps const jumps = ChainHealForecast::Of(bot, world, *m, o.spell, o.castSec);
                    p.otherHeal = jumps.heal;
                    p.otherEffective = jumps.effective;
                }
                c.options.push_back(p);
                c.spells.push_back(o.spell);
            }
        };
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
            c.guid = m.guid;
            c.healthPct = m.healthPct;
            c.deathPenalty = Value::DeathPenalty(m);
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
            c.rawHealth = c.target.health;
            c.settled = c.hotTarget == HotPlan::HotTarget::Member && !EnemyOn(world, m.guid);
            if (c.settled)
                c.target.health = std::min(c.target.maxHealth, c.target.health + HotSurplus(world, m));
            c.target.chained = directives.IsHealChain(m.guid, now, tickMs + CHAIN_GAP_MS);
            addOptions(c, choice, &m);
            c.eat = EatOf(bot, unit, m);
            c.target.waitDeficit = HotPlan::WaitDeficit(c.eat);
            c.target.boostDeficit = BoostWait(bot, c.options);
            c.assessment = HealPlan::Assess(c.target, healer, c.options);
            all.push_back(std::move(c));
        }
        // Allies the party protects (AllyFacts::protect, boss doc G3): healed like a member, by
        // the same need rules. Their intake is only the enemies on them now (Value::IncomingOn:
        // no measured rate, no timeline of casts, no HoTs or shields known on them), so the
        // time to die is health / intake, the worst case the same.
        for (AllyFacts const& a : world.allies)
        {
            if (!a.protect || !a.health)
                continue;
            Unit* unit = me->GetMap()->GetUnit(a.guid);
            if (!unit || !unit->IsAlive() || !bot->IsValidHealTarget(unit, healer.steady ? 101.0f : 100.0f))
                continue;
            HealChoice choice;
            bot->ChooseHeal(unit, choice, false);
            if (choice.options.empty())
                continue;

            Candidate c;
            c.unit = unit;
            c.guid = a.guid;
            c.healthPct = a.healthPct;
            c.deathPenalty = Value::DeathPenalty(a);
            c.hotTarget = HotPlan::HotTarget::Member;
            c.target.health = float(unit->GetHealth());
            c.target.maxHealth = float(unit->GetMaxHealth());
            c.rawHealth = c.target.health;
            c.target.intake = Value::IncomingOn(world, a);
            if (c.target.intake > 0.0f)
            {
                c.target.timeToDie = c.target.health / c.target.intake;
                c.target.worstTimeToDie = c.target.timeToDie;
                c.target.timeToReserve = std::max(0.0f, c.target.health - HealPlan::ReserveHealth(c.target)) / c.target.intake;
            }
            else
                c.target.timeToDie = c.target.worstTimeToDie = c.target.timeToReserve = HealPlan::NO_LIMIT;
            c.target.chained = directives.IsHealChain(a.guid, now, tickMs + CHAIN_GAP_MS);
            addOptions(c, choice, nullptr);
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
                p.action.target = c.guid;
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
        // A party heal before the single-target heals, also when no single gap reached its
        // deficit yet (several small gaps together).
        if (!heldByPull && ProposePartyHeal(bot, world, healer, all, checkAggro, out))
            return;
        for (Candidate* pc : candidates)
        {
            Candidate& c = *pc;
            bool const emergency = c.assessment.need == HealPlan::Need::Emergency;
            if (heldByPull && !emergency)
                continue;

            // Swiftmend first (instant; in an emergency before Nature's Swiftness), then the
            // healer's HoT on a lift target, then the heal.
            HotPlan::EatUse const eatUse = HotPlan::Decide(c.eat, c.RawMissing(), c.assessment.need);
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
            if (ProposeShield(bot, world, c.unit, c.guid, c.target, c.assessment,
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
            p.action.target = c.guid;
            p.action.spell = c.spells[index];
            p.action.prepSpell = boostSpell;
            // Benefit: the health it restores, plus the death it prevents when the target would
            // not last until the next chance to heal it, plus the mana a free boost saves.
            // Cost: the overheal. A chained heal's jumps count on both sides (Option::otherHeal).
            heal += o.otherHeal;
            effective += o.otherEffective;
            float benefit = Value::Points(world, effective);
            float nextChanceSec = std::max(HealPlan::GCD_SEC, castSec) + healer.tickSec * 2.0f;
            if (healer.lockoutStartSec >= 0.0f)     // the last heal before the healer's lockout
                nextChanceSec = std::max(nextChanceSec, healer.NextChanceSec(castSec));
            if (emergency && c.assessment.timeToDie < nextChanceSec)
                benefit += c.deathPenalty;
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
            HotPlan::EatUse const eatUse = HotPlan::Decide(pc->eat, pc->RawMissing(), pc->assessment.need);
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
