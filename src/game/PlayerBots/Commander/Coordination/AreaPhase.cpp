#include "Commander/Coordination/AreaPhase.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/AreaValue.h"
#include "Commander/Perception/ManaWorth.h"
#include "Commander/Perception/PackResolver.h"
#include "Commander/Perception/Tanks.h"
#include "Commander/Perception/ValueModel.h"
#include "Commander/Perception/WorldModel.h"
#include <algorithm>
#include <cmath>

namespace Commander
{
    namespace
    {
        bool PartyHolds(TeamPlan const& plan)
        {
            if (plan.retreat.active)
                return true;
            PullPlan const& pull = plan.pull;
            if (pull.phase == PullPhase::Approach || pull.phase == PullPhase::Return)
                return true;
            return pull.phase == PullPhase::Settle &&
                   (pull.awaitArrival || pull.holdDamage || pull.losBreak == PullLosBreak::Active);
        }

        std::vector<ObjectGuid> AllTanks(WorldModel const& world)
        {
            std::vector<ObjectGuid> out;
            for (PartyMemberFacts const& m : world.party)
                if (!m.isPet && m.alive && Tanks::IsTank(m))
                    out.push_back(m.guid);
            return out;
        }

        // Points a healer bot's shield on the caster adds to `v` (and who), 0 = none worth it.
        void AllyShield(WorldModel const& world, AreaValue::Request r, AreaValue::Result const& v, AreaPlan& a)
        {
            if (!v.valid || v.turnLoss <= 0.0f)
                return;
            PartyMemberFacts const& caster = *r.caster;
            for (PartyMemberFacts const& h : world.party)
            {
                if (!h.isBot || h.isPet || !h.CanAct() || h.guid == caster.guid)
                    continue;
                for (SurvivalAbility const& s : h.survival)
                {
                    if (s.effect != SurvivalAbility::Effect::Absorb || s.selfOnly || !s.ready ||
                        PackResolver::Distance(h.pos, r.from) > s.maxRange)
                        continue;
                    if (s.mechanic && s.mechanic < 32 && (caster.immuneMechanics & (1u << s.mechanic)))
                        continue;
                    AreaValue::Request with = r;
                    with.turn.extraAbsorb += s.amount;
                    AreaValue::Result const w = AreaValue::Evaluate(world, with);
                    if (!w.valid)
                        continue;
                    float const gain = w.net - v.net - Value::LostAction(world, h, Value::GCD_SEC) -
                                       ManaWorth::Points(world, h, float(s.manaCost));
                    if (gain > a.allyShieldGain)
                    {
                        a.allyShieldGain = gain;
                        a.allyShieldBot = h.guid;
                        a.allyShieldSpellId = s.spellId;
                    }
                }
            }
        }

        // What the phase loses with every tank busy, and without the taunts.
        // `walkCost`: already in v.net (a spot to walk to), not in the requests' results.
        void Reserves(WorldModel const& world, AreaValue::Request const& r, AreaValue::Result const& v,
                      float walkCost, std::vector<ObjectGuid> const& tanks, AreaPlan& a)
        {
            float const net = std::max(0.0f, v.net);
            AreaValue::Request busy = r;
            busy.turn.busyTanks = tanks;
            AreaValue::Result const b = AreaValue::Evaluate(world, busy);
            a.endCost = std::max(0.0f, net - std::max(0.0f, b.valid ? b.net - walkCost : 0.0f));
            AreaValue::Request bare = r;
            bare.turn.noTaunts = true;
            AreaValue::Result const t = AreaValue::Evaluate(world, bare);
            a.tauntReserve = std::max(0.0f, net - std::max(0.0f, t.valid ? t.net - walkCost : 0.0f));
        }

        struct Candidate
        {
            AreaValue::Request request;
            AreaValue::Result value;
            float score = 0.0f;
            float netHere = 0.0f;
            bool move = false;
            float moveBenefit = 0.0f;
            float moveCost = 0.0f;
            float shieldGain = 0.0f;
            ObjectGuid centre;
        };
    }

    void AreaPhaseAssigner::Assign(WorldModel const& world, TeamPlan& plan) const
    {
        AreaPlan const prev = plan.area;
        AreaPlan& a = plan.area;
        a = AreaPlan();
        if (!world.HasPartyEnemy() || PartyHolds(plan))
            return;

        std::vector<ObjectGuid> const tanks = AllTanks(world);
        // Tanks walking off a patrol's path (PatrolAssigner, this refresh or the last one).
        std::vector<ObjectGuid> busy;
        if (plan.patrolMove.active && plan.patrolMove.phase == PatrolPhase::Move)
            busy.push_back(plan.patrolMove.tank);
        auto const forbidden = [&plan](EnemyFacts const& e) { return plan.KeepsUnbroken(e); };

        // 1. A channel running.
        for (PartyMemberFacts const& m : world.party)
        {
            if (!m.isBot || m.isPet || !m.areaChannel.spellId)
                continue;
            AreaSpellFacts const* spell = AreaValue::SpellOf(m, m.areaChannel.spellId);
            if (!spell)
                continue;
            AreaValue::Request r;
            r.caster = &m;
            r.spell = spell;
            r.centre = m.areaChannel.centre;
            r.from = m.pos;
            r.ticks = m.areaChannel.ticksLeft;
            r.paid = true;
            r.turn.busyTanks = busy;
            r.forbidden = forbidden;
            AreaValue::Result const v = AreaValue::Evaluate(world, r);
            a.active = true;
            a.step = AreaStep::Channel;
            a.caster = m.guid;
            a.spellId = m.areaChannel.spellId;
            a.centrePos = m.areaChannel.centre;
            a.benefit = v.benefit;
            a.cost = v.cost;
            a.net = v.net;
            a.netHere = v.net;
            a.targets = v.targets;
            a.sinceMs = prev.CastBy(m.guid) ? prev.sinceMs : world.refreshedAtMs;
            Reserves(world, r, v, 0.0f, tanks, a);
            AllyShield(world, r, v, a);
            return;
        }

        // 2. Who starts which one, where.
        Candidate best;
        bool found = false;
        for (PartyMemberFacts const& m : world.party)
        {
            if (!m.isBot || m.isPet || !m.CanAct() || m.areaSpells.empty() ||
                (m.role != ROLE_RANGE_DPS && m.role != ROLE_MELEE_DPS))
                continue;
            for (AreaSpellFacts const& spell : m.areaSpells)
            {
                if (!spell.ready)
                    continue;
                std::vector<EnemyFacts const*> centres;
                if (spell.onTarget)
                {
                    for (EnemyFacts const& e : world.enemies)
                        if (e.acting && !e.crowdControlled && !e.onAllyOnly && e.health)
                            centres.push_back(&e);
                }
                else
                    centres.push_back(nullptr);

                for (EnemyFacts const* e : centres)
                {
                    Candidate c;
                    c.request.caster = &m;
                    c.request.spell = &spell;
                    c.request.from = m.pos;
                    c.request.centre = e ? e->pos : m.pos;
                    c.request.turn.busyTanks = busy;
                    c.request.forbidden = forbidden;
                    c.centre = e ? e->guid : ObjectGuid();
                    float const dist = e ? PackResolver::Distance(m.pos, e->pos) : 0.0f;
                    bool const inRange = !e || dist <= spell.range;

                    AreaValue::Result here;
                    if (inRange)
                        here = AreaValue::Evaluate(world, c.request);
                    c.netHere = here.valid ? here.net : 0.0f;
                    c.value = here;

                    // Farther back: a ground channel from as far as it reaches.
                    float const want = spell.range - SPOT_MARGIN_YD;
                    if (e && spell.channel && dist > 0.1f && want - dist >= SPOT_MIN_GAIN_YD)
                    {
                        AreaValue::Request back = c.request;
                        float const k = want / dist;
                        back.from.x = e->pos.x + (m.pos.x - e->pos.x) * k;
                        back.from.y = e->pos.y + (m.pos.y - e->pos.y) * k;
                        back.from.z = m.pos.z;
                        AreaValue::Result const v = AreaValue::Evaluate(world, back);
                        float const walkCost = Value::LostAction(world, m, (want - dist) / RUN_YD_PER_SEC);
                        if (v.valid && (!here.valid || v.net - walkCost > here.net))
                        {
                            c.request = back;
                            c.value = v;
                            c.move = true;
                            c.moveBenefit = here.valid ? std::max(0.0f, here.turnLoss - v.turnLoss) : v.net;
                            c.moveCost = walkCost;
                            c.value.cost += walkCost;
                            c.value.net -= walkCost;
                        }
                    }
                    if (!c.value.valid)
                        continue;

                    // Its own shield first.
                    SelfShieldFacts const& sh = m.selfShield;
                    if (sh.spellId && sh.ready && !sh.up && c.value.turnLoss > 0.0f)
                    {
                        AreaValue::Request with = c.request;
                        with.turn.extraAbsorb += sh.absorb;
                        AreaValue::Result const v = AreaValue::Evaluate(world, with);
                        if (v.valid)
                        {
                            float const gain = (v.net - (c.move ? c.moveCost : 0.0f)) - c.value.net -
                                               Value::LostAction(world, m, Value::GCD_SEC) -
                                               ManaWorth::Points(world, m, float(sh.manaCost));
                            if (gain > 0.0f)
                            {
                                c.shieldGain = gain;
                                c.request = with;
                            }
                        }
                    }

                    c.score = c.value.net + c.shieldGain;
                    if (prev.active && prev.caster == m.guid && prev.spellId == spell.spellId && prev.centre == c.centre)
                        c.score += STICKY_POINTS;
                    if (c.value.net + c.shieldGain > 0.0f && (!found || c.score > best.score))
                    {
                        best = c;
                        found = true;
                    }
                }
            }
        }
        if (!found)
            return;

        PartyMemberFacts const& m = *best.request.caster;
        a.active = true;
        a.caster = m.guid;
        a.spellId = best.request.spell->spellId;
        a.centre = best.centre;
        a.centrePos = best.request.centre;
        a.spot = best.request.from;
        a.moveBenefit = best.moveBenefit;
        a.moveCost = best.moveCost;
        a.netHere = best.netHere;
        a.benefit = best.value.benefit;
        a.cost = best.value.cost;
        a.net = best.value.net + best.shieldGain;
        a.targets = best.value.targets;
        if (best.shieldGain > 0.0f)
        {
            a.shieldSpellId = m.selfShield.spellId;
            a.shieldGain = best.shieldGain;
        }
        a.step = best.move ? AreaStep::Move : best.shieldGain > 0.0f ? AreaStep::Shield : AreaStep::Cast;
        a.sinceMs = (prev.active && prev.caster == m.guid && prev.spellId == a.spellId) ? prev.sinceMs : world.refreshedAtMs;
        Reserves(world, best.request, best.value, best.moveCost, tanks, a);
        AllyShield(world, best.request, best.value, a);
    }

    void AreaPhaseAssigner::Clear(TeamPlan& plan) const
    {
        plan.area = AreaPlan();
    }
}
