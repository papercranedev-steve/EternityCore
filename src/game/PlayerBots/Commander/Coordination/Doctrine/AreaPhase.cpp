#include "Commander/Coordination/Doctrine/AreaPhase.h"
#include "Commander/Coordination/Doctrine/AreaIntent.h"
#include "Commander/Coordination/Doctrine/AreaPlace.h"
#include "Commander/Perception/Forecast/Runners.h"
#include "Commander/Coordination/Doctrine/AreaSpot.h"
#include "Commander/Coordination/Doctrine/TacticalDoctrine.h"
#include "Commander/Coordination/Doctrine/ThreatOpening.h"
#include "Commander/Perception/Spatial/PositionField.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/Forecast/ThreatGap.h"
#include "Commander/Perception/Forecast/ThreatRunway.h"
#include "Commander/Perception/Values/AreaValue.h"
#include "Commander/Perception/Profiles/Tags.h"
#include "Commander/Perception/Values/ManaWorth.h"
#include "Commander/Perception/Spatial/PackResolver.h"
#include "Commander/Perception/Tanks.h"
#include "Commander/Perception/Values/ValueModel.h"
#include "Commander/Perception/WorldModel.h"
#include "Timer.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

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
            // The threat opening first (ThreatOpening.h, registered before this module: this
            // refresh's): the tank builds threat on what came, the area spell waits.
            if (plan.threat.active)
                return true;
            return pull.phase == PullPhase::Settle &&
                   (pull.awaitArrival || pull.losBreak == PullLosBreak::Active);
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
            bool shield = false;            // its own shield first
            float shieldGain = 0.0f;
            ObjectGuid centre;
            std::vector<Position3> spots;   // move: AreaSpot's cells, best first
        };

        std::string Text(char const* fmt, float a, float b = 0.0f)
        {
            char buf[200];
            snprintf(buf, sizeof(buf), fmt, a, b);
            return buf;
        }

        // The last refresh's spot of `m` for `centre` (the area plan's Move, or its prep spot).
        Position3 const* PrevSpot(AreaPlan const& prev, ObjectGuid m, Position3 const& centre, ObjectGuid centreGuid,
                                  uint32 spellId)
        {
            if (prev.active && prev.caster == m && prev.step == AreaStep::Move && prev.spellId == spellId &&
                prev.centre == centreGuid)
                return &prev.spot;
            if (prev.prepCaster == m && !prev.prepSpots.empty() &&
                PackResolver::Distance(centre, prev.prepCentre) <= 2.0f * PositionField::CELL_YD)
                return &prev.prepSpots.front();
            return nullptr;
        }

        void SetPrepSpot(AreaPlan& a, ObjectGuid caster, Position3 const& centre, AreaSpot::Choice const& ch)
        {
            a.prepCaster = caster;
            a.prepCentre = centre;
            a.prepSpots = ch.spots;
            a.prepMoveGain = std::max(0.0f, ch.gain + ch.walkCost);
            a.prepMoveCost = ch.walkCost;
        }

        // The caster the Area tactic counts on (header "who"), null = none.
        PartyMemberFacts const* PlannedCaster(WorldModel const& world, TeamPlan const& plan)
        {
            DoctrinePlan const& d = plan.doctrine;
            PullPlan const& pull = plan.pull;
            // Before the fight there is no tactic yet; a pull of AREA_MIN or more counts as Area.
            std::vector<PlannedThreat> const casters = d.tactic == PileTactic::Area ? d.areaThreat
                : (pull.phase != PullPhase::None && pull.expected.size() >= TacticalDoctrine::AREA_MIN)
                    ? AreaIntent::Of(world, nullptr) : std::vector<PlannedThreat>();
            PartyMemberFacts const* m = nullptr;
            float bestRate = 0.0f;
            for (PlannedThreat const& p : casters)
                if (PartyMemberFacts const* f = world.FindMember(p.member))
                    if (f->CanAct() && p.perSec > bestRate)
                    {
                        m = f;
                        bestRate = p.perSec;
                    }
            return m;
        }

        // It can hold what runs at it around itself (Frost Nova: FleeStopAbility with a radius).
        bool HoldsAround(PartyMemberFacts const& m)
        {
            return std::any_of(m.fleeStops.begin(), m.fleeStops.end(),
                               [](FleeStopAbility const& f) { return f.radius > 0.0f && f.holdSec > 0.0f; });
        }

        // An enemy hits or runs at `m` (rooted ones too: they come when the root ends).
        bool Chased(WorldModel const& world, ObjectGuid m)
        {
            return std::any_of(world.enemies.begin(), world.enemies.end(),
                               [m](EnemyFacts const& e) { return e.acting && e.health && e.victim == m; });
        }

        // Header "room": the area caster now, else the one waiting or getting ready, else the
        // one the tactic counts on; the last refresh's one keeps it while chased.
        ObjectGuid RoomCaster(WorldModel const& world, TeamPlan const& plan, AreaPlan const& prev, AreaPlan const& a)
        {
            ObjectGuid g = a.active ? a.caster : !a.waitCaster.IsEmpty() ? a.waitCaster : a.prepCaster;
            if (g.IsEmpty() && !prev.roomCaster.IsEmpty() && Chased(world, prev.roomCaster))
                g = prev.roomCaster;
            if (g.IsEmpty())
                if (PartyMemberFacts const* m = PlannedCaster(world, plan))
                    g = m->guid;
            PartyMemberFacts const* m = g.IsEmpty() ? nullptr : world.FindMember(g);
            return m && m->isBot && !m->isPet && m->alive && HoldsAround(*m) ? g : ObjectGuid();
        }

        // While the pull or the opening holds the party (not a retreat): the caster the Area
        // tactic counts on gets ready (header "While the pull or the opening holds").
        void Prepare(WorldModel const& world, TeamPlan const& plan, AreaPlan const& prev, AreaPlan& a)
        {
            if (plan.retreat.active)
                return;
            PullPlan const& pull = plan.pull;
            PartyMemberFacts const* m = PlannedCaster(world, plan);
            AreaSpellFacts const* spell = m ? AreaIntent::BestSpell(*m) : nullptr;
            if (!spell)
                return;

            // Where the spell will go: the enemy on the main tank nearest it, else the planned spot.
            PartyMemberFacts const* tank = Tanks::Main(world, true);
            EnemyFacts const* centre = nullptr;
            float nearest = 0.0f;
            for (EnemyFacts const& e : world.enemies)
            {
                if (!tank || !e.acting || e.crowdControlled || e.onAllyOnly || !e.health || e.victim != tank->guid)
                    continue;
                float const distance = PackResolver::Distance(tank->pos, e.pos);
                if (!centre || distance < nearest)
                {
                    centre = &e;
                    nearest = distance;
                }
            }
            Position3 at;
            if (centre)
                at = centre->pos;
            else if (!pull.PlannedFightSpot(at))
                return;

            AreaValue::Request r;
            r.caster = m;
            r.spell = spell;
            r.centre = spell->onTarget ? at : m->pos;
            r.from = m->pos;
            r.settle = false;
            r.forbidden = [&plan](EnemyFacts const& e) { return plan.KeepsUnbroken(e); };

            // Its spot (a ground channel).
            if (spell->onTarget && spell->channel)
            {
                AreaSpot::Choice const ch = AreaSpot::Pick(world, plan, r, centre != nullptr,
                    PrevSpot(prev, m->guid, r.centre, centre ? centre->guid : ObjectGuid(), spell->spellId));
                if (ch.move)
                {
                    SetPrepSpot(a, m->guid, r.centre, ch);
                    r.from = ch.spots.front();
                }
            }

            // Its shield, whenever it is ready (user 2026-11-12, doc .kiro/docs/2026-11-12-mage-hold.md
            // 3.6: on cooldown in the area tactic; until then only when the spell valued with
            // it gained more than its mana). Worth the health it takes.
            SelfShieldFacts const& sh = m->selfShield;
            if (!sh.spellId || !sh.ready || sh.up)
                return;
            a.prepCaster = m->guid;
            a.prepShieldId = sh.spellId;
            a.prepGain = Value::Points(world, sh.absorb);
        }
        // Seconds the main tank needs to give every enemy `c` turns a runway through the whole
        // spell (ThreatRunway shortfalls with the spell's threat, one after another at
        // ThreatGap::FillRate), and their total. False: no main tank, or one is not on it.
        bool TankFill(WorldModel const& world, Candidate const& c, float& sec, float& total)
        {
            PartyMemberFacts const* tank = Tanks::Main(world, true);
            AreaSpellFacts const& s = *c.request.spell;
            if (!tank || !tank->CanAct() || c.value.turning.empty() || s.periodSec <= 0.0f)
                return false;
            std::vector<PlannedThreat> const planned{ { c.request.caster->guid, s.tickThreat / s.periodSec } };
            float const spellSec = float(s.ticks) * s.periodSec;
            total = 0.0f;
            for (ObjectGuid g : c.value.turning)
            {
                EnemyFacts const* e = world.FindEnemy(g);
                if (!e || e->victim != tank->guid)
                    return false;
                total += ThreatRunway::Of(world, *e, tank->guid, planned, spellSec).shortfall;
            }
            sec = total / ThreatGap::FillRate(world, *tank);
            return true;
        }

        // Step 3: the best spell is worth more a little later (doc .kiro/docs/2026-11-11-aoe-threat.md
        // 4.3, 4.4). A wait of `sec` costs the share of the later net the spell would have
        // made by then (net x sec / its length). Later:
        //   Tank    nobody turns (net + turn loss) once the tank has filled their shortfalls;
        //           not when the total did not shrink for ThreatOpeningAssigner::STALL_MS.
        //   Shield  the caster's shield (on cooldown now) first, as in step 2.
        // Writes the wait into `a` (active stays false); a note when it starts.
        bool Waits(WorldModel const& world, Candidate const& c, AreaPlan const& prev, AreaPlan& a,
                   std::vector<std::string>& notes)
        {
            AreaSpellFacts const& s = *c.request.spell;
            PartyMemberFacts const& m = *c.request.caster;
            float const spellSec = float(s.ticks) * s.periodSec;
            if (spellSec <= 0.0f || c.value.turnLoss <= 0.0f)
                return false;
            float const now = c.value.net + c.shieldGain;
            uint32 const nowMs = world.refreshedAtMs;
            bool const waitedTank = prev.wait == AreaWait::Tank && prev.waitCaster == m.guid;

            AreaWait wait = AreaWait::None;
            float gain = 0.0f;
            float sec = 0.0f;
            float fillSec = 0.0f;
            float total = 0.0f;
            if (TankFill(world, c, fillSec, total))
            {
                bool const stalled = waitedTank && total >= prev.waitShortfall - ThreatOpeningAssigner::PROGRESS_MIN &&
                                     WorldTimer::getMSTimeDiff(prev.waitProgressMs, nowMs) > ThreatOpeningAssigner::STALL_MS;
                float const later = c.value.net + c.value.turnLoss;
                float const g = later - std::max(0.0f, later) * fillSec / spellSec - now;
                if (!stalled && g > gain)
                {
                    wait = AreaWait::Tank;
                    gain = g;
                    sec = fillSec;
                }
            }
            SelfShieldFacts const& sh = m.selfShield;
            if (sh.spellId && !sh.up && !sh.ready && sh.cooldownSec > 0.0f)
            {
                AreaValue::Request with = c.request;
                with.turn.extraAbsorb += sh.absorb;
                AreaValue::Result const v = AreaValue::Evaluate(world, with);
                if (v.valid)
                {
                    float const later = v.net - (c.move ? c.moveCost : 0.0f) -
                                        Value::LostAction(world, m, Value::GCD_SEC) -
                                        ManaWorth::Points(world, m, float(sh.manaCost));
                    float const g = later - std::max(0.0f, later) * sh.cooldownSec / spellSec - now;
                    if (g > gain)
                    {
                        wait = AreaWait::Shield;
                        gain = g;
                        sec = sh.cooldownSec;
                    }
                }
            }
            if (wait == AreaWait::None)
                return false;

            bool const same = prev.wait == wait && prev.waitCaster == m.guid;
            a.wait = wait;
            a.waitCaster = m.guid;
            a.waitSec = sec;
            a.waitGain = gain;
            a.waitSinceMs = same ? prev.waitSinceMs : nowMs;
            if (wait == AreaWait::Tank)
            {
                // Progress as the opening's: the smallest total so far and when it shrank.
                bool const progress = !waitedTank || total < prev.waitShortfall - ThreatOpeningAssigner::PROGRESS_MIN;
                a.waitShortfall = progress ? total : prev.waitShortfall;
                a.waitProgressMs = progress ? nowMs : prev.waitProgressMs;
            }
            if (!same)
                notes.push_back(wait == AreaWait::Tank
                    ? Text("Area spell waits ~%.0f s: the tank first gives the %.0f enemies it would turn a runway through it.",
                           sec, float(c.value.turning.size()))
                    : Text("Area spell waits %.0f s for the caster's shield (%.1f points better).", sec, gain));
            return true;
        }
    }

    void AreaPhaseAssigner::Assign(WorldModel const& world, TeamPlan& plan) const
    {
        AreaPlan const prev = plan.area;
        Plan(world, plan, prev);
        // Reads the last refresh's own roomCaster (RoomCaster), not another module's.
        plan.area.roomCaster = world.HasPartyEnemy() ? RoomCaster(world, plan, prev, plan.area) : ObjectGuid();
    }

    void AreaPhaseAssigner::Plan(WorldModel const& world, TeamPlan& plan, AreaPlan const& prev)
    {
        AreaPlan& a = plan.area;
        a = AreaPlan();
        if (!world.HasPartyEnemy())
            return;
        if (PartyHolds(plan))
        {
            Prepare(world, plan, prev, a);
            return;
        }

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
            // Enemies running at it: a ground channel goes ahead of the first (AreaPlace.h).
            std::vector<Runners::Runner> const runners = Runners::Of(world, m.guid, m.pos);
            for (AreaSpellFacts const& spell : m.areaSpells)
            {
                if (!spell.ready)
                    continue;
                bool const ground = spell.onTarget && spell.channel;
                std::vector<EnemyFacts const*> centres;
                if (ground && !runners.empty())
                    centres.push_back(runners.front().facts);
                else if (spell.onTarget)
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
                    if (e && ground)
                    {
                        // A ground channel: the user's placement rules (AreaPlace.h).
                        AreaPlace::Choice const ch = runners.empty()
                            ? AreaPlace::OnPile(world, plan, c.request, *e,
                                                PrevSpot(prev, m.guid, e->pos, c.centre, spell.spellId))
                            : AreaPlace::OnLead(world, c.request, *e);
                        if (!ch.valid)
                            continue;
                        c.request = ch.request;
                        c.netHere = ch.netHere;
                        c.value = ch.value;
                        if (ch.move)
                        {
                            c.spots = ch.spots;
                            c.move = true;
                            c.moveBenefit = std::max(0.0f, ch.gain + ch.walkCost);
                            c.moveCost = ch.walkCost;
                            c.value.cost += ch.walkCost;
                            c.value.net -= ch.walkCost;
                        }
                    }
                    else if (!e || PackResolver::Distance(m.pos, e->pos) <= spell.range)
                    {
                        c.value = AreaValue::Evaluate(world, c.request);
                        c.netHere = c.value.valid ? c.value.net : 0.0f;
                    }
                    if (!c.value.valid)
                        continue;
                    // Never a neutral (yellow) creature in the area (user 2026-11-10; the cast
                    // gate Constraints::CastForbidden would refuse it anyway).
                    if (Tags::NeutralNear(world, c.request.centre, spell.radius))
                        continue;

                    // Its own shield first, whenever it is ready (user 2026-11-12, doc
                    // .kiro/docs/2026-11-12-mage-hold.md 3.6: on cooldown before every area
                    // spell; until then only when it gained more than its GCD and mana). Its
                    // gain only counts for the score when positive.
                    SelfShieldFacts const& sh = m.selfShield;
                    if (sh.spellId && sh.ready && !sh.up)
                    {
                        AreaValue::Request with = c.request;
                        with.turn.extraAbsorb += sh.absorb;
                        AreaValue::Result const v = AreaValue::Evaluate(world, with);
                        if (v.valid)
                        {
                            float const gain = (v.net - (c.move ? c.moveCost : 0.0f)) - c.value.net -
                                               Value::LostAction(world, m, Value::GCD_SEC) -
                                               ManaWorth::Points(world, m, float(sh.manaCost));
                            c.shield = true;
                            c.shieldGain = std::max(0.0f, gain);
                            c.request = with;
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

        // 3. Or a little later.
        if (Waits(world, best, prev, a, plan.notes))
        {
            // Meanwhile it walks to where it will cast from.
            if (best.move)
            {
                AreaSpot::Choice ch;
                ch.spots = best.spots;
                ch.walkCost = best.moveCost;
                ch.gain = best.moveBenefit - best.moveCost;
                SetPrepSpot(a, best.request.caster->guid, best.request.centre, ch);
            }
            return;
        }

        PartyMemberFacts const& m = *best.request.caster;
        a.active = true;
        a.caster = m.guid;
        a.spellId = best.request.spell->spellId;
        a.centre = best.centre;
        a.centrePos = best.request.centre;
        a.spot = best.request.from;
        a.spots = best.spots;
        a.moveBenefit = best.moveBenefit;
        a.moveCost = best.moveCost;
        a.netHere = best.netHere;
        a.benefit = best.value.benefit;
        a.cost = best.value.cost;
        a.net = best.value.net + best.shieldGain;
        a.targets = best.value.targets;
        if (best.shield)
        {
            a.shieldSpellId = m.selfShield.spellId;
            a.shieldGain = best.shieldGain;
        }
        a.step = best.move ? AreaStep::Move : best.shield ? AreaStep::Shield : AreaStep::Cast;
        a.sinceMs = (prev.active && prev.caster == m.guid && prev.spellId == a.spellId) ? prev.sinceMs : world.refreshedAtMs;
        Reserves(world, best.request, best.value, best.moveCost, tanks, a);
        AllyShield(world, best.request, best.value, a);
    }

    void AreaPhaseAssigner::Clear(TeamPlan& plan) const
    {
        plan.area = AreaPlan();
    }
}
