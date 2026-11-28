#include "Commander/Perception/Values/AreaValue.h"
#include "Commander/Perception/Values/LooseEnemy.h"
#include "Commander/Perception/Values/ManaWorth.h"
#include "Commander/Perception/Values/SurvivalValue.h"
#include "Commander/Perception/Tanks.h"
#include "Commander/Perception/Spatial/PackResolver.h"
#include "Commander/Perception/Forecast/Runners.h"
#include "Commander/Perception/Forecast/ThreatRoom.h"
#include "Commander/Perception/Forecast/ThreatView.h"
#include "Commander/Perception/Values/ValueModel.h"
#include "SpellMgr.h"
#include <algorithm>
#include <cmath>

namespace Commander
{
namespace AreaValue
{
    namespace
    {
        struct Target
        {
            EnemyFacts const* facts = nullptr;
            bool free = false;
            float myThreat = 0.0f;
            float holderThreat = 0.0f;
            float factor = ThreatView::RANGED_AGGRO_FACTOR;
            float speed = 0.0f;
            PartyMemberFacts const* holder = nullptr;
            Position3 pos;      // when the spell starts (header "Later")
        };


        // An enemy hitting the caster in melee from `fromSec` after the start on.
        struct Hitter
        {
            EnemyFacts const* facts = nullptr;
            float fromSec = 0.0f;
        };

        // Seconds from `at` until `e` (at `pos`, running at `speed`) is in melee reach of it.
        float WalkSec(EnemyFacts const& e, Position3 const& pos, Position3 const& at, float speed)
        {
            float const yards = std::max(0.0f, PackResolver::Distance(at, pos) - e.boundingRadius - MELEE_REACH);
            return yards / (speed > 0.1f ? speed : AggroTurnCost::RUN_YARDS_PER_SEC);
        }

        // Seconds of a channel lost to its hits (Unit::DealDamage -> Spell::DelayedChannel: a hit
        // the absorbs do not take whole takes 1.0, 0.8, 0.6, 0.4, 0.2 s and then 0.2 s each off
        // what is left, Spell::GetNextDelayAtDamageMsTime). `absorb`: the caster's absorbs and a
        // shield put up first; armor is not counted (a hit uses up more of the shield than it
        // does). Melee swings only (meleeHit every meleeHit / meleeDps).
        float PushbackSec(std::vector<Hitter> const& hitters, float channelSec, float absorb)
        {
            struct Hit
            {
                float at;
                float damage;
            };
            std::vector<Hit> hits;
            for (Hitter const& h : hitters)
            {
                EnemyFacts const& e = *h.facts;
                if (!e.meleeEnabled || e.meleeHit <= 0.0f || e.meleeDps <= 0.0f)
                    continue;
                float const swing = std::max(0.5f, e.meleeHit / e.meleeDps);
                for (float t = h.fromSec; t < channelSec; t += swing)
                    hits.push_back({ t, e.meleeHit });
            }
            std::sort(hits.begin(), hits.end(), [](Hit const& a, Hit const& b) { return a.at < b.at; });
            float end = channelSec;
            uint32 delays = 0;
            for (Hit const& hit : hits)
            {
                if (hit.at >= end)
                    break;
                if (absorb >= hit.damage)
                {
                    absorb -= hit.damage;
                    continue;
                }
                absorb = 0.0f;
                float const delay = delays < 5 ? 1.0f - 0.2f * float(delays) : 0.2f;
                ++delays;
                end = std::max(hit.at, end - delay);
            }
            return channelSec - end;
        }
    }

    float WalkSecUnder(Position3 const& from, float speed, float slowPct, Position3 const& centre, float radius,
                       Position3 const& to, float reach)
    {
        speed = std::max(0.1f, speed);
        float const dx = to.x - from.x, dy = to.y - from.y;
        float const length = std::sqrt(dx * dx + dy * dy);
        float const walk = std::max(0.0f, length - reach);
        if (slowPct <= 0.0f || walk <= 0.0f)
            return walk / speed;
        // Yards of the walk inside the circle: from `from` along the line to where it leaves.
        float inside = 0.0f;
        float const fx = from.x - centre.x, fy = from.y - centre.y;
        if (fx * fx + fy * fy <= radius * radius && length > 0.01f)
        {
            float const ux = dx / length, uy = dy / length;
            float const b = fx * ux + fy * uy;
            float const c = fx * fx + fy * fy - radius * radius;
            inside = std::min(walk, -b + std::sqrt(std::max(0.0f, b * b - c)));
        }
        float const slow = speed * (1.0f - slowPct);
        float sec = inside / slow;
        float rest = walk - inside;
        float const linger = std::min(rest, slow * SLOW_LINGER_SEC);
        sec += linger / slow;
        rest -= linger;
        return sec + rest / speed;
    }

    namespace
    {
        // SpeedTo with `e` at `pos` (Request::inSec: where it will be).
        float SpeedFrom(EnemyFacts const& e, Position3 const& pos, AreaSpellFacts const& spell, Position3 const& centre,
                        Position3 const& to)
        {
            if (spell.slowPct <= 0.0f || e.slowed)
                return e.speed;
            float const reach = e.boundingRadius + MELEE_REACH;
            float const walk = std::max(0.0f, PackResolver::Distance(pos, to) - reach);
            if (walk <= 0.0f)
                return e.speed;
            float const sec = WalkSecUnder(pos, e.speed, spell.slowPct, centre, spell.radius, to, reach);
            return sec > 0.0f ? walk / sec : e.speed;
        }
    }

    float SpeedTo(EnemyFacts const& e, AreaSpellFacts const& spell, Position3 const& centre, Position3 const& to)
    {
        return SpeedFrom(e, e.pos, spell, centre, to);
    }

    AreaSpellFacts const* SpellOf(PartyMemberFacts const& caster, uint32 spellId)
    {
        uint32 const first = sSpellMgr.GetFirstSpellInChain(spellId);
        for (AreaSpellFacts const& a : caster.areaSpells)
            if (a.spellId == spellId || sSpellMgr.GetFirstSpellInChain(a.spellId) == first)
                return &a;
        return nullptr;
    }

    Result Evaluate(WorldModel const& world, Request const& r)
    {
        Result out;
        out.invalid = "no caster, spell or filler";
        if (!r.caster || !r.spell || r.caster->fillerDps <= 0.0f)
            return out;
        PartyMemberFacts const& self = *r.caster;
        AreaSpellFacts const& spell = *r.spell;
        uint32 const count = r.ticks ? std::min(r.ticks, spell.ticks) : spell.ticks;
        out.invalid = "no ticks";
        if (!count)
            return out;

        // Where each enemy is when the spell starts (header "Later").
        auto const posOf = [&](EnemyFacts const& e)
        {
            return r.inSec > 0.0f ? Runners::PosAfter(world, e, r.inSec, self.guid, r.from) : e.pos;
        };

        // Targets.
        std::vector<Target> targets;
        for (EnemyFacts const& e : world.enemies)
        {
            if (!e.acting || e.onAllyOnly || !e.health)
                continue;
            Position3 const pos = posOf(e);
            if (PackResolver::Distance(r.centre, pos) - e.boundingRadius > spell.radius)
                continue;
            if (r.forbidden && r.forbidden(e))
            {
                out.invalid = "a kept control in the area";
                return out;
            }
            if (e.crowdControlled)
                continue;
            Target t;
            t.facts = &e;
            t.pos = pos;
            t.free = e.victim.IsEmpty() || e.victim == self.guid || (r.noHold && r.noHold(e));
            if (!t.free)
            {
                // The tank has not built threat on a new enemy yet.
                if (!r.paid && r.settle && ThreatRoom::Settling(world, e))
                {
                    out.invalid = "a new enemy in the area (the tank has no threat on it yet)";
                    return out;
                }
                ThreatEntry const* mine = ThreatView::FindEntry(e, self.guid);
                t.myThreat = (mine ? mine->threat : 0.0f) + (self.boltTarget == e.guid ? self.boltThreat : 0.0f);
                t.holderThreat = e.victimThreat;
                bool const melee = PackResolver::Distance(r.from, pos) - e.boundingRadius <= MELEE_REACH;
                t.factor = melee ? ThreatView::MELEE_AGGRO_FACTOR : ThreatView::RANGED_AGGRO_FACTOR;
                t.holder = world.FindMember(e.victim);
                t.speed = SpeedFrom(e, pos, spell, r.centre, r.from);
            }
            targets.push_back(t);
            out.targets.push_back(e.guid);
        }
        out.invalid = "no target in the area";
        if (targets.empty())
            return out;

        // On the caster already (header "On the caster"): only while a tank is alive to take
        // them back (no tank: nobody else would, the spell's threat changes nothing).
        struct OnCaster
        {
            EnemyFacts const* facts = nullptr;
            float myThreat = 0.0f;
            float tankThreat = 0.0f;
            float tankWalkSec = 0.0f;
            float extraDps = 0.0f;
        };
        std::vector<OnCaster> onCaster;
        PartyMemberFacts const* tank = Tanks::Main(world);
        if (tank && tank->guid != self.guid)
        {
            bool anyOnMe = false;
            for (Target const& t : targets)
            {
                EnemyFacts const& e = *t.facts;
                if (e.victim != self.guid)
                    continue;
                anyOnMe = true;
                // Fodder stays on it whatever the spell does (header "Fodder").
                if (r.noHold && r.noHold(e))
                    continue;
                OnCaster c;
                c.facts = &e;
                ThreatEntry const* mine = ThreatView::FindEntry(e, self.guid);
                ThreatEntry const* tanks = ThreatView::FindEntry(e, tank->guid);
                c.myThreat = (mine ? mine->threat : 0.0f) + (self.boltTarget == e.guid ? self.boltThreat : 0.0f);
                c.tankThreat = tanks ? tanks->threat : 0.0f;
                c.tankWalkSec = std::max(0.0f, PackResolver::Distance(tank->pos, t.pos) - MELEE_REACH) /
                                AggroTurnCost::RUN_YARDS_PER_SEC;
                c.extraDps = std::max(0.0f, Value::HitDps(e, self) - Value::HitDps(e, *tank));
                onCaster.push_back(c);
            }
            if (!r.paid && anyOnMe)
            {
                float const busySec = std::max(Value::GCD_SEC, float(count) * spell.periodSec);
                SurvivalValue::Danger const danger = SurvivalValue::Of(world, self);
                // The shield put up first takes hits before its health (Request::turn.extraAbsorb).
                float const ttd = r.turn.extraAbsorb > 0.0f
                    ? danger.timeline.TimeToLose(float(self.health) + self.absorb + r.turn.extraAbsorb, false,
                                                 danger.Healers())
                    : danger.ttd;
                if (ttd < SurvivalValue::DANGER_SEC + busySec)
                {
                    out.invalid = "the caster is in danger under the enemies on it";
                    return out;
                }
            }
        }
        out.invalid = nullptr;

        // Tick plan.
        std::vector<float> damage(count, 0.0f);
        struct Turning
        {
            uint32 tick = 0;
            AggroTurnCost::Turn turn;
            float hitLoss = 0.0f;
            ObjectGuid guid;
            float arriveSec = 0.0f;     // in melee reach of the caster (from the start)
        };
        std::vector<Turning> turning;
        for (Target const& t : targets)
        {
            float const health = float(t.facts->health);
            // Header "Others": their damage comes off its health first.
            float otherPerTick = 0.0f;
            for (OtherDamage const& o : r.others)
                if (o.enemy == t.facts->guid)
                    otherPerTick += o.perSec * spell.periodSec;
            // healthAfter[n - 1]: its health after n ticks (theirs, then mine, each tick).
            std::vector<float> healthAfter(count, 0.0f);
            float hp = health;
            float dealt = 0.0f;
            for (uint32 n = 1; n <= count; ++n)
            {
                hp = std::max(0.0f, hp - otherPerTick);
                float const d = std::min(spell.perTick, hp);
                hp -= d;
                dealt += d;
                damage[n - 1] += dealt;
                healthAfter[n - 1] = hp;
            }
            if (t.free)
                continue;
            for (uint32 k = 1; k <= count; ++k)
            {
                if (healthAfter[k - 1] <= 0.0f)
                    break;      // the tick that kills it turns nothing
                float const mine = t.myThreat + float(k) * spell.tickThreat;
                // The holder adds nothing during the spell: the tank cannot hold (header; until
                // 2026-11-12 half its measured rate, ThreatRunway::HolderRate).
                float const holder = t.holderThreat;
                if (mine <= holder * t.factor)
                    continue;
                Turning x;
                x.tick = k;
                x.guid = t.facts->guid;
                x.arriveSec = float(k) * spell.periodSec + WalkSec(*t.facts, t.pos, r.from, t.speed);
                if (t.holder)
                {
                    // The holder (a melee tank) then needs 110% of mine to take it back.
                    x.turn.enemy = t.facts;
                    x.turn.holder = t.holder;
                    x.turn.speed = t.speed;
                    x.turn.gap = std::max(0.0f, mine * ThreatView::MELEE_AGGRO_FACTOR - holder);
                }
                else
                    x.hitLoss = Value::HitLoss(world, *t.facts, self,
                                               std::min(AggroTurnCost::HORIZON_SEC, Value::EnemyTimeToDie(world, *t.facts)));
                turning.push_back(x);
                break;
            }
        }

        // Header "At most N targets": the server hits a random maxTargets of them. Damage: the
        // expected share; the turns stay as if each were hit (any of them may be).
        if (spell.maxTargets && targets.size() > spell.maxTargets)
        {
            float const share = float(spell.maxTargets) / float(targets.size());
            for (float& d : damage)
                d *= share;
        }

        // Enemies on the caster already hit it from their walk on (a channel only: pushback).
        std::vector<Hitter> onMe;
        if (spell.channel)
            for (EnemyFacts const& e : world.enemies)
                if (e.acting && !e.crowdControlled && e.health && e.victim == self.guid)
                {
                    Position3 const pos = posOf(e);
                    onMe.push_back({ &e, WalkSec(e, pos, r.from, SpeedFrom(e, pos, spell, r.centre, r.from)) });
                }

        // The longer the enemies on the caster stay there after n ticks of the spell's threat
        // (the tank wins them back one after another at its measured rate; taunts not counted).
        float const tankRate = tank ? LooseEnemy::TankRate(world, tank->guid) : 0.0f;
        auto const keptOnCaster = [&](uint32 n)
        {
            if (onCaster.empty())
                return 0.0f;
            std::vector<float> before, after;
            for (OnCaster const& c : onCaster)
            {
                before.push_back(std::max(0.0f, c.myThreat * ThreatView::MELEE_AGGRO_FACTOR - c.tankThreat));
                after.push_back(std::max(0.0f, (c.myThreat + float(n) * spell.tickThreat) * ThreatView::MELEE_AGGRO_FACTOR -
                                               c.tankThreat));
            }
            std::vector<float> const secBefore = LooseEnemy::SequentialRecovery(before, tankRate);
            std::vector<float> const secAfter = LooseEnemy::SequentialRecovery(after, tankRate);
            float loss = 0.0f;
            for (size_t i = 0; i < onCaster.size(); ++i)
            {
                OnCaster const& c = onCaster[i];
                float const horizon = std::min(AggroTurnCost::HORIZON_SEC, Value::EnemyTimeToDie(world, *c.facts));
                float const endBefore = std::min(horizon, c.tankWalkSec + secBefore[i]);
                float const endAfter = std::min(horizon, c.tankWalkSec + secAfter[i]);
                float const longer = std::max(0.0f, endAfter - endBefore);
                if (longer > 0.0f)
                    loss += Value::Points(world, c.extraDps * longer) + Value::HarassLoss(world, *c.facts, self, longer);
            }
            return loss;
        };

        AggroTurnCost::Options options = r.turn;
        options.at = &r.from;
        float const fillerPerSec = self.fillerDps;
        float const fillerManaPerSec = r.paid ? self.fillerManaPerSec : 0.0f;
        std::vector<AggroTurnCost::Turn> group;
        std::vector<Hitter> hitters;
        auto const valueOf = [&](uint32 n, float& benefit, float& cost, float& turnLoss)
        {
            group.clear();
            hitters = onMe;
            turnLoss = 0.0f;
            for (Turning const& x : turning)
            {
                if (x.tick > n)
                    continue;
                if (x.turn.enemy)
                    group.push_back(x.turn);
                else
                    turnLoss += x.hitLoss;
                if (spell.channel)
                    if (EnemyFacts const* e = world.FindEnemy(x.guid))
                        hitters.push_back({ e, x.arriveSec });
            }
            if (!group.empty())
                turnLoss += AggroTurnCost::OfGroup(world, group, self, options);
            turnLoss += keptOnCaster(n);
            float const sec = float(n) * spell.periodSec;
            benefit = Value::LostAction(world, self, damage[n - 1] / fillerPerSec) +
                      (fillerManaPerSec > 0.0f ? ManaWorth::Points(world, self, fillerManaPerSec * sec) : 0.0f);
            float const timeCost = Value::LostAction(world, self, sec);
            // The ticks the hits push off the end (a shield that holds keeps them: Ice Barrier,
            // "spells will not be interrupted"): that share of the spell's worth beyond its time.
            if (!hitters.empty() && sec > 0.0f)
            {
                float const lost = PushbackSec(hitters, sec, self.absorb + options.extraAbsorb);
                turnLoss += std::max(0.0f, benefit - timeCost) * lost / sec;
            }
            cost = timeCost + turnLoss;
        };

        auto const finish = [&](uint32 n, float benefit, float cost, float turnLoss)
        {
            out.ticks = n;
            out.damage = damage[n - 1];
            out.benefit = benefit;
            out.cost = cost;
            out.turnLoss = turnLoss;
            out.net = benefit - cost;
            out.turning.clear();
            for (Turning const& x : turning)
                if (x.tick <= n)
                    out.turning.push_back(x.guid);
        };

        if (r.paid)
        {
            // Going on: the best number of the ticks left.
            bool any = false;
            for (uint32 n = 1; n <= count; ++n)
            {
                float b = 0.0f, c = 0.0f, l = 0.0f;
                valueOf(n, b, c, l);
                if (!any || b - c > out.net)
                {
                    finish(n, b, c, l);
                    any = true;
                }
            }
            out.valid = any;
            if (!any)
                out.invalid = "no ticks";
            return out;
        }

        // Starting: the whole spell, its mana included.
        float b = 0.0f, c = 0.0f, l = 0.0f;
        valueOf(count, b, c, l);
        float const busySec = float(count) * spell.periodSec;
        // Not less damage per mana than the filler (no fight length guess: adds come unannounced).
        if (spell.mana > 0.0f && self.fillerManaPerSec > 0.0f &&
            damage[count - 1] / spell.mana < fillerPerSec / self.fillerManaPerSec)
        {
            out.invalid = "less damage per mana than the filler";
            return out;
        }
        float const extraMana = std::max(0.0f, spell.mana - self.fillerManaPerSec * busySec);
        out.mana = extraMana > 0.0f ? ManaWorth::Points(world, self, extraMana) : 0.0f;
        // Header "Rage and energy": the same two rules with the filler's own power.
        out.power = 0.0f;
        if (spell.power > 0.0f && self.fillerPowerPerSec > 0.0f && spell.powerType == self.fillerPowerType)
        {
            if (damage[count - 1] / spell.power < fillerPerSec / self.fillerPowerPerSec)
            {
                out.invalid = "less damage per rage or energy than the filler";
                return out;
            }
            float const extraPower = std::max(0.0f, spell.power - self.fillerPowerPerSec * busySec);
            out.power = extraPower > 0.0f ? Value::LostAction(world, self, extraPower / self.fillerPowerPerSec) : 0.0f;
        }
        finish(count, b, c + out.mana + out.power, l);
        out.valid = true;
        return out;
    }
}
}
