#include "Commander/Coordination/Doctrine/AreaPhase.h"
#include "Commander/Coordination/Doctrine/AreaIntent.h"
#include "Commander/Coordination/Doctrine/AreaPlace.h"
#include "Commander/Perception/Forecast/Runners.h"
#include "Commander/Coordination/Doctrine/AreaSpot.h"
#include "Commander/Coordination/Doctrine/TacticalDoctrine.h"
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
#include "Commander/Trace/Trace.h"
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
            // The pack still coming (PullPlan::packComing). Whether the tank's threat is enough
            // is the wait below (AreaWait::Tank; until 2026-11-13 the threat opening held it too).
            return pull.phase == PullPhase::Settle &&
                   (pull.awaitArrival || pull.packComing || pull.losBreak == PullLosBreak::Active);
        }

        // AreaValue::Request::noHold from the doctrine (TeamPlan::AreaFodder).
        std::function<bool(EnemyFacts const&)> NoHoldOf(TeamPlan const& plan)
        {
            return [&plan](EnemyFacts const& e) { return plan.AreaFodder(e); };
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
        void AllyShield(WorldModel const& world, AreaValue::Request r, AreaValue::Result const& v, AreaCast& a)
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

        // What the spell loses with every tank busy, and without the taunts.
        // `walkCost`: already in v.net (a spot to walk to), not in the requests' results.
        void Reserves(WorldModel const& world, AreaValue::Request const& r, AreaValue::Result const& v,
                      float walkCost, std::vector<ObjectGuid> const& tanks, AreaCast& a)
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

        // What the casts planned so far put on the enemies (header "Several casters"): their
        // damage (AreaValue::Request::others) and their threat (the tank's wait).
        struct Planned
        {
            std::vector<AreaValue::OtherDamage> damage;
            std::vector<PlannedThreat> threat;

            void Add(AreaCast const& c, AreaSpellFacts const& spell)
            {
                if (spell.periodSec <= 0.0f)
                    return;
                for (ObjectGuid e : c.targets)
                    damage.push_back({ e, spell.perTick / spell.periodSec });
                threat.push_back({ c.caster, spell.tickThreat / spell.periodSec });
            }
        };

        std::string Text(char const* fmt, float a, float b = 0.0f)
        {
            char buf[200];
            snprintf(buf, sizeof(buf), fmt, a, b);
            return buf;
        }

        // The last refresh's spot of `m` for `centre` (its spell's Move, or its prep spot).
        Position3 const* PrevSpot(AreaPlan const& prev, ObjectGuid m, Position3 const& centre, ObjectGuid centreGuid,
                                  uint32 spellId)
        {
            if (AreaCast const* c = prev.CastOf(m))
                if (c->step == AreaStep::Move && c->spellId == spellId && c->centre == centreGuid)
                    return &c->spot;
            if (AreaPrep const* p = prev.PrepOf(m))
                if (!p->spots.empty() && PackResolver::Distance(centre, p->centre) <= 2.0f * PositionField::CELL_YD)
                    return &p->spots.front();
            return nullptr;
        }

        AreaPrep& PrepFor(AreaPlan& a, ObjectGuid caster)
        {
            for (AreaPrep& p : a.preps)
                if (p.caster == caster)
                    return p;
            a.preps.emplace_back();
            a.preps.back().caster = caster;
            return a.preps.back();
        }

        void SetPrepSpot(AreaPlan& a, ObjectGuid caster, Position3 const& centre, AreaSpot::Choice const& ch)
        {
            AreaPrep& p = PrepFor(a, caster);
            p.centre = centre;
            p.spots = ch.spots;
            p.moveGain = std::max(0.0f, ch.gain + ch.walkCost);
            p.moveCost = ch.walkCost;
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

        // Header "room": the area casters now, waiting or getting ready; the last refresh's
        // ones keep it while chased; none of them: the ones the tactic counts on.
        std::vector<ObjectGuid> RoomCasters(WorldModel const& world, TeamPlan const& plan, AreaPlan const& prev,
                                            AreaPlan const& a)
        {
            std::vector<ObjectGuid> named;
            auto const add = [&named](ObjectGuid g)
            {
                if (!g.IsEmpty() && std::find(named.begin(), named.end(), g) == named.end())
                    named.push_back(g);
            };
            for (AreaCast const& c : a.casts)
                add(c.caster);
            for (AreaWaitPlan const& w : a.waits)
                add(w.caster);
            for (AreaPrep const& p : a.preps)
                add(p.caster);
            for (ObjectGuid g : prev.roomCasters)
                if (Chased(world, g))
                    add(g);
            if (named.empty())
                for (PartyMemberFacts const* m : AreaIntent::PlannedCasters(world, plan))
                    add(m->guid);
            std::vector<ObjectGuid> out;
            for (ObjectGuid g : named)
            {
                PartyMemberFacts const* m = world.FindMember(g);
                if (m && m->isBot && !m->isPet && m->alive && HoldsAround(*m))
                    out.push_back(g);
            }
            return out;
        }

        // While the pull holds the party (not a retreat): every caster the Area tactic counts
        // on gets ready (header "While the pull holds").
        void Prepare(WorldModel const& world, TeamPlan const& plan, AreaPlan const& prev, AreaPlan& a)
        {
            if (plan.retreat.active)
                return;
            std::vector<PartyMemberFacts const*> const casters = AreaIntent::PlannedCasters(world, plan);
            if (casters.empty())
                return;

            // Where the spells will go: the enemy on the main tank nearest it, else the planned spot.
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
            else if (!plan.PlannedFightSpot(at))
                return;

            for (PartyMemberFacts const* m : casters)
            {
                AreaSpellFacts const* spell = AreaIntent::BestSpell(*m);
                if (!spell)
                    continue;
                AreaValue::Request r;
                r.caster = m;
                r.spell = spell;
                r.centre = spell->onTarget ? at : m->pos;
                r.from = m->pos;
                r.settle = false;
                r.forbidden = [&plan](EnemyFacts const& e) { return plan.AreaKeepsUnbroken(e); };
                r.noHold = NoHoldOf(plan);

                // Its spot (a ground channel).
                if (spell->onTarget && spell->channel)
                {
                    AreaSpot::Choice const ch = AreaSpot::Pick(world, plan, r, centre != nullptr,
                        PrevSpot(prev, m->guid, r.centre, centre ? centre->guid : ObjectGuid(), spell->spellId));
                    if (ch.move)
                        SetPrepSpot(a, m->guid, r.centre, ch);
                }

                // Its shield, whenever it is ready (user 2026-11-12, doc .kiro/docs/2026-11-12-mage-hold.md
                // 3.6: on cooldown in the area tactic; until then only when the spell valued with
                // it gained more than its mana). Worth the health it takes.
                SelfShieldFacts const& sh = m->selfShield;
                if (!sh.spellId || !sh.ready || sh.up)
                    continue;
                AreaPrep& p = PrepFor(a, m->guid);
                p.shieldId = sh.spellId;
                p.gain = Value::Points(world, sh.absorb);
            }
        }

        // Seconds the holders need to give every enemy `c` turns a runway through the whole
        // spell (ThreatRunway shortfalls with the spell's threat and the planned casts', one
        // after another at ThreatGap::FillRate), and their total. The holders: the main tank,
        // and an off-tank for the enemy it holds (doctrine OffTank.h, user 2026-11-15: the
        // hunter's pet in the area pile); they fill at the same time, the slowest one counts.
        // False: no main tank, or one is on neither.
        bool TankFill(WorldModel const& world, DoctrinePlan const& doctrine, Candidate const& c, Planned const& planned,
                      float& sec, float& total)
        {
            PartyMemberFacts const* tank = Tanks::Main(world, true);
            AreaSpellFacts const& s = *c.request.spell;
            if (!tank || !tank->CanAct() || c.value.turning.empty() || s.periodSec <= 0.0f)
                return false;
            std::vector<PlannedThreat> threat = planned.threat;
            threat.push_back({ c.request.caster->guid, s.tickThreat / s.periodSec });
            float const spellSec = float(s.ticks) * s.periodSec;
            total = 0.0f;
            std::vector<std::pair<PartyMemberFacts const*, float>> holders;     // holder, its shortfalls
            for (ObjectGuid g : c.value.turning)
            {
                EnemyFacts const* e = world.FindEnemy(g);
                if (!e)
                    return false;
                PartyMemberFacts const* holder = e->victim == tank->guid ? tank : nullptr;
                if (!holder && doctrine.OffTankHolds(e->victim, e->guid))
                    holder = world.FindMember(e->victim);
                if (!holder || !holder->CanAct())
                    return false;
                float const shortfall = ThreatRunway::Of(world, *e, holder->guid, threat, spellSec).shortfall;
                total += shortfall;
                auto h = std::find_if(holders.begin(), holders.end(), [holder](auto const& x) { return x.first == holder; });
                if (h == holders.end())
                    holders.push_back({ holder, shortfall });
                else
                    h->second += shortfall;
            }
            sec = 0.0f;
            for (auto const& h : holders)
                sec = std::max(sec, h.second / ThreatGap::FillRate(world, *h.first));
            return true;
        }

        // Step 3: the best spell is worth more a little later (doc .kiro/docs/2026-11-11-aoe-threat.md
        // 4.3, 4.4). A wait of `sec` costs the share of the later net the spell would have
        // made by then (net x sec / its length). Later:
        //   Tank    nobody turns (net + turn loss) once the tank has filled their shortfalls;
        //           not when the total did not shrink for TANK_STALL_MS.
        //   Shield  the caster's shield (on cooldown now) first, as in step 2.
        // Adds the caster's wait to `a.waits`; a note when it starts.
        bool Waits(WorldModel const& world, DoctrinePlan const& doctrine, Candidate const& c, Planned const& planned,
                   AreaPlan const& prev, AreaPlan& a, std::vector<std::string>& notes)
        {
            AreaSpellFacts const& s = *c.request.spell;
            PartyMemberFacts const& m = *c.request.caster;
            float const spellSec = float(s.ticks) * s.periodSec;
            if (spellSec <= 0.0f || c.value.turnLoss <= 0.0f)
                return false;
            float const now = c.value.net + c.shieldGain;
            uint32 const nowMs = world.refreshedAtMs;
            AreaWaitPlan const* before = prev.WaitOf(m.guid);
            bool const waitedTank = before && before->wait == AreaWait::Tank;

            AreaWait wait = AreaWait::None;
            float gain = 0.0f;
            float sec = 0.0f;
            float fillSec = 0.0f;
            float total = 0.0f;
            if (TankFill(world, doctrine, c, planned, fillSec, total))
            {
                bool const stalled = waitedTank && total >= before->shortfall - AreaPhaseAssigner::TANK_PROGRESS_MIN &&
                                     WorldTimer::getMSTimeDiff(before->progressMs, nowMs) > AreaPhaseAssigner::TANK_STALL_MS;
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

            bool const same = before && before->wait == wait;
            AreaWaitPlan w;
            w.wait = wait;
            w.caster = m.guid;
            w.sec = sec;
            w.gain = gain;
            w.sinceMs = same ? before->sinceMs : nowMs;
            if (wait == AreaWait::Tank)
            {
                // Progress: the smallest total so far and when it shrank.
                bool const progress = !waitedTank || total < before->shortfall - AreaPhaseAssigner::TANK_PROGRESS_MIN;
                w.shortfall = progress ? total : before->shortfall;
                w.progressMs = progress ? nowMs : before->progressMs;
            }
            a.waits.push_back(w);
            if (!same)
                notes.push_back(wait == AreaWait::Tank
                    ? Text("Area spell waits ~%.0f s: the tank first gives the %.0f enemies it would turn a runway through it.",
                           sec, float(c.value.turning.size()))
                    : Text("Area spell waits %.0f s for the caster's shield (%.1f points better).", sec, gain));
            return true;
        }

        // Header "Reservation": the casters this refresh names (a spell to start, a wait, the
        // prep), and the last ones for RESERVE_HOLD_MS while they can still cast an area spell.
        void Reserve(WorldModel const& world, AreaPlan const& prev, AreaPlan& a)
        {
            auto const add = [&a](ObjectGuid g, uint32 atMs)
            {
                if (!g.IsEmpty() && !a.Reserves(g))
                    a.reserved.push_back({ g, atMs });
            };
            for (AreaCast const& c : a.casts)
                if (c.step != AreaStep::Channel)
                    add(c.caster, world.refreshedAtMs);
            for (AreaWaitPlan const& w : a.waits)
                add(w.caster, world.refreshedAtMs);
            for (AreaPrep const& p : a.preps)
                add(p.caster, world.refreshedAtMs);
            if (!world.HasPartyEnemy())
                return;
            for (AreaReservation const& r : prev.reserved)
            {
                // Its channel running is its spell itself; after it the next refresh decides afresh.
                if (a.Reserves(r.caster) || a.CastBy(r.caster) ||
                    WorldTimer::getMSTimeDiff(r.atMs, world.refreshedAtMs) > AreaPhaseAssigner::RESERVE_HOLD_MS)
                    continue;
                PartyMemberFacts const* m = world.FindMember(r.caster);
                if (!m || !m->CanAct() ||
                    std::none_of(m->areaSpells.begin(), m->areaSpells.end(), [](AreaSpellFacts const& s) { return s.ready; }))
                    continue;
                a.reserved.push_back(r);
            }
        }

        // The calculation trace: one step-2 candidate (or why there is none for it).
        void TraceCandidate(PartyMemberFacts const& m, AreaSpellFacts const& spell, EnemyFacts const* centre,
                            char const* why, Candidate const* c)
        {
            if (!Trace::On())
                return;
            std::string const label = Trace::Format("%s spell%u on %s", Trace::Guid(m.guid).c_str(), spell.spellId,
                                                    centre ? Trace::Guid(centre->guid).c_str() : "self");
            std::string data = Trace::Format("{\"why\":\"%s\"", why);
            if (c)
            {
                AreaValue::Result const& v = c->value;
                data += Trace::Format(",\"net\":%.2f,\"benefit\":%.2f,\"cost\":%.2f,\"turnLoss\":%.2f,\"mana\":%.2f,\"power\":%.2f,"
                                      "\"targets\":%u,\"turning\":%u,\"shieldGain\":%.2f,\"move\":%s,\"moveCost\":%.2f,"
                                      "\"netHere\":%.2f,\"score\":%.2f",
                    v.net, v.benefit, v.cost, v.turnLoss, v.mana, v.power, uint32(v.targets.size()), uint32(v.turning.size()),
                    c->shieldGain, c->move ? "true" : "false", c->moveCost, c->netHere, c->score);
            }
            Trace::Emit("areacand", label, data + "}");
        }

        // Step 2 for one caster: its best paying candidate (every ready area spell, every
        // centre), valued with the casts planned so far. False: none pays.
        bool BestOf(WorldModel const& world, TeamPlan const& plan, AreaPlan const& prev, PartyMemberFacts const& m,
                    Planned const& planned, std::vector<ObjectGuid> const& busy,
                    std::function<bool(EnemyFacts const&)> const& forbidden, Candidate& best)
        {
            bool found = false;
            AreaCast const* prevCast = prev.CastOf(m.guid);
            // Enemies running at it: a ground channel goes ahead of the first (AreaPlace.h).
            std::vector<Runners::Runner> const runners = Runners::Of(world, m.guid, m.pos);
            for (AreaSpellFacts const& spell : m.areaSpells)
            {
                if (!spell.ready)
                {
                    TraceCandidate(m, spell, nullptr, "not ready", nullptr);
                    continue;
                }
                bool const ground = spell.onTarget && spell.channel;
                // The tank taking the pile elsewhere: a circle put on it now is left behind
                // (strat-20261114-172715: a Blizzard cast while the tank walked 6 yd off for a
                // shackle hit 2 of 4 for its last ticks). Started once it is there. Reads
                // plan.fightSpot: this refresh's (FightSpotAssigner runs before) or, in another
                // order, the last one's; both are right.
                if (ground && plan.fightSpot.moving)
                {
                    TraceCandidate(m, spell, nullptr, "the tank moves the fight", nullptr);
                    continue;
                }
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
                    c.request.noHold = NoHoldOf(plan);
                    c.request.others = planned.damage;
                    c.centre = e ? e->guid : ObjectGuid();
                    if (e && ground)
                    {
                        // A ground channel: the user's placement rules (AreaPlace.h).
                        AreaPlace::Choice const ch = runners.empty()
                            ? AreaPlace::OnPile(world, plan, c.request, *e,
                                                PrevSpot(prev, m.guid, e->pos, c.centre, spell.spellId))
                            : AreaPlace::OnLead(world, c.request, *e);
                        if (!ch.valid)
                        {
                            TraceCandidate(m, spell, e, ch.value.invalid ? ch.value.invalid
                                : runners.empty() ? "no place on the pile (AreaPlace::OnPile)"
                                : "no place ahead of the runner (AreaPlace::OnLead)", nullptr);
                            continue;
                        }
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
                    {
                        TraceCandidate(m, spell, e, c.value.invalid ? c.value.invalid : "out of range", nullptr);
                        continue;
                    }
                    // Never a neutral (yellow) creature in the area (user 2026-11-10; the cast
                    // gate Constraints::CastForbidden would refuse it anyway).
                    if (Tags::NeutralNear(world, c.request.centre, spell.radius))
                    {
                        TraceCandidate(m, spell, e, "a neutral creature in the area", nullptr);
                        continue;
                    }

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
                    if (prevCast && prevCast->spellId == spell.spellId && prevCast->centre == c.centre)
                        c.score += AreaPhaseAssigner::STICKY_POINTS;
                    bool const pays = c.value.net + c.shieldGain > 0.0f;
                    TraceCandidate(m, spell, e, pays ? "pays" : "does not pay", &c);
                    if (pays && (!found || c.score > best.score))
                    {
                        best = c;
                        found = true;
                    }
                }
            }
            return found;
        }
    }

    void AreaPhaseAssigner::Assign(WorldModel const& world, TeamPlan& plan) const
    {
        Trace::Scope scope("area");
        AreaPlan const prev = plan.area;
        char const* const outcome = Plan(world, plan, prev);
        AreaPlan& a = plan.area;
        // Reads the last refresh's own roomCasters (RoomCasters), not another module's.
        a.roomCasters = world.HasPartyEnemy() ? RoomCasters(world, plan, prev, a) : std::vector<ObjectGuid>();
        // Reads the last refresh's own reservations.
        Reserve(world, prev, a);
        if (scope.On())
        {
            scope.SetLabel(outcome);
            std::string data = "{\"casts\":[";
            for (size_t i = 0; i < a.casts.size(); ++i)
            {
                AreaCast const& c = a.casts[i];
                data += Trace::Format("%s{\"caster\":\"%s\",\"spell\":%u,\"step\":%u,\"net\":%.2f}", i ? "," : "",
                    Trace::Guid(c.caster).c_str(), c.spellId, uint32(c.step), c.net);
            }
            data += "],\"waits\":[";
            for (size_t i = 0; i < a.waits.size(); ++i)
            {
                AreaWaitPlan const& w = a.waits[i];
                data += Trace::Format("%s{\"caster\":\"%s\",\"wait\":%u,\"gain\":%.2f,\"sec\":%.1f}", i ? "," : "",
                    Trace::Guid(w.caster).c_str(), uint32(w.wait), w.gain, w.sec);
            }
            data += "],\"prep\":[";
            for (size_t i = 0; i < a.preps.size(); ++i)
                data += Trace::Format("%s\"%s\"", i ? "," : "", Trace::Guid(a.preps[i].caster).c_str());
            data += "],\"reserved\":[";
            for (size_t i = 0; i < a.reserved.size(); ++i)
                data += Trace::Format("%s{\"caster\":\"%s\",\"agoMs\":%u}", i ? "," : "",
                    Trace::Guid(a.reserved[i].caster).c_str(), WorldTimer::getMSTimeDiff(a.reserved[i].atMs, world.refreshedAtMs));
            scope.Data(data + "]}");
        }
    }

    char const* AreaPhaseAssigner::Plan(WorldModel const& world, TeamPlan& plan, AreaPlan const& prev)
    {
        AreaPlan& a = plan.area;
        a = AreaPlan();
        if (!world.HasPartyEnemy())
            return "no fight";
        if (PartyHolds(plan))
        {
            Prepare(world, plan, prev, a);
            return "the party holds: prep";
        }

        std::vector<ObjectGuid> const tanks = AllTanks(world);
        // Tanks moving the fight (FightSpotAssigner, this refresh: it runs before).
        std::vector<ObjectGuid> busy;
        if (plan.fightSpot.moving)
            busy.push_back(plan.fightSpot.tank);
        std::function<bool(EnemyFacts const&)> const forbidden = [&plan](EnemyFacts const& e) { return plan.AreaKeepsUnbroken(e); };
        Planned planned;

        // 1. The channels running: each one is its caster's spell.
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
            r.noHold = NoHoldOf(plan);
            r.others = planned.damage;
            AreaValue::Result const v = AreaValue::Evaluate(world, r);
            AreaCast c;
            c.step = AreaStep::Channel;
            c.caster = m.guid;
            c.spellId = m.areaChannel.spellId;
            c.centrePos = m.areaChannel.centre;
            c.benefit = v.benefit;
            c.cost = v.cost;
            c.net = v.net;
            c.netHere = v.net;
            c.targets = v.targets;
            AreaCast const* before = prev.CastOf(m.guid);
            c.sinceMs = before ? before->sinceMs : world.refreshedAtMs;
            Reserves(world, r, v, 0.0f, tanks, c);
            AllyShield(world, r, v, c);
            a.casts.push_back(c);
            planned.Add(a.casts.back(), *spell);
        }

        // 2. Every other area dealer, the best paying spell first, each valued with the ones
        // planned before it (header "Several casters"); 3. or it waits.
        for (;;)
        {
            Candidate best;
            bool found = false;
            for (PartyMemberFacts const& m : world.party)
            {
                if (!m.isBot || m.isPet || !m.CanAct() || m.areaSpells.empty() ||
                    (m.role != ROLE_RANGE_DPS && m.role != ROLE_MELEE_DPS) ||
                    a.CastBy(m.guid) || a.WaitOf(m.guid))
                    continue;
                Candidate c;
                if (BestOf(world, plan, prev, m, planned, busy, forbidden, c) && (!found || c.score > best.score))
                {
                    best = c;
                    found = true;
                }
            }
            if (!found)
                break;

            PartyMemberFacts const& m = *best.request.caster;
            if (Waits(world, plan.doctrine, best, planned, prev, a, plan.notes))
            {
                // Meanwhile it walks to where it will cast from.
                if (best.move)
                {
                    AreaSpot::Choice ch;
                    ch.spots = best.spots;
                    ch.walkCost = best.moveCost;
                    ch.gain = best.moveBenefit - best.moveCost;
                    SetPrepSpot(a, m.guid, best.request.centre, ch);
                }
                continue;
            }

            AreaCast c;
            c.caster = m.guid;
            c.spellId = best.request.spell->spellId;
            c.centre = best.centre;
            c.centrePos = best.request.centre;
            c.spot = best.request.from;
            c.spots = best.spots;
            c.moveBenefit = best.moveBenefit;
            c.moveCost = best.moveCost;
            c.netHere = best.netHere;
            c.benefit = best.value.benefit;
            c.cost = best.value.cost;
            c.net = best.value.net + best.shieldGain;
            c.targets = best.value.targets;
            if (best.shield)
            {
                c.shieldSpellId = m.selfShield.spellId;
                c.shieldGain = best.shieldGain;
            }
            c.step = best.move ? AreaStep::Move : best.shield ? AreaStep::Shield : AreaStep::Cast;
            AreaCast const* before = prev.CastOf(m.guid);
            c.sinceMs = (before && before->spellId == c.spellId) ? before->sinceMs : world.refreshedAtMs;
            Reserves(world, best.request, best.value, best.moveCost, tanks, c);
            AllyShield(world, best.request, best.value, c);
            a.casts.push_back(c);
            planned.Add(a.casts.back(), *best.request.spell);
        }

        if (a.casts.empty())
        {
            if (a.waits.empty())
                return "none: no area spell pays";
            return a.waits.front().wait == AreaWait::Tank ? "waits: the tank first" : "waits: the shield first";
        }
        switch (a.casts.back().step)
        {
            case AreaStep::Move:    return "start: walk to the spot";
            case AreaStep::Shield:  return "start: shield first";
            case AreaStep::Cast:    return "start: cast";
            default:                return "a channel runs";
        }
    }

    void AreaPhaseAssigner::Clear(TeamPlan& plan) const
    {
        plan.area = AreaPlan();
    }
}
