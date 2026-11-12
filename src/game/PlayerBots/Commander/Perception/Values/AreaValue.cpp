#include "Commander/Perception/Values/AreaValue.h"
#include "Commander/Perception/Values/ManaWorth.h"
#include "Commander/Perception/Spatial/PackResolver.h"
#include "Commander/Perception/Forecast/ThreatRoom.h"
#include "Commander/Perception/Forecast/ThreatRunway.h"
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
            float holderRate = 0.0f;
            float factor = ThreatView::RANGED_AGGRO_FACTOR;
            float speed = 0.0f;
            PartyMemberFacts const* holder = nullptr;
        };


        // An enemy hitting the caster in melee from `fromSec` after the start on.
        struct Hitter
        {
            EnemyFacts const* facts = nullptr;
            float fromSec = 0.0f;
        };

        // Seconds from `at` until `e` (running at `speed`) is in melee reach of it.
        float WalkSec(EnemyFacts const& e, Position3 const& at, float speed)
        {
            float const yards = std::max(0.0f, PackResolver::Distance(at, e.pos) - e.boundingRadius - MELEE_REACH);
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

    float SpeedTo(EnemyFacts const& e, AreaSpellFacts const& spell, Position3 const& centre, Position3 const& to)
    {
        if (spell.slowPct <= 0.0f || e.slowed)
            return e.speed;
        float const reach = e.boundingRadius + MELEE_REACH;
        float const walk = std::max(0.0f, PackResolver::Distance(e.pos, to) - reach);
        if (walk <= 0.0f)
            return e.speed;
        float const sec = WalkSecUnder(e.pos, e.speed, spell.slowPct, centre, spell.radius, to, reach);
        return sec > 0.0f ? walk / sec : e.speed;
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
        if (!r.caster || !r.spell || r.caster->fillerDps <= 0.0f)
            return out;
        PartyMemberFacts const& self = *r.caster;
        AreaSpellFacts const& spell = *r.spell;
        uint32 const count = r.ticks ? std::min(r.ticks, spell.ticks) : spell.ticks;
        if (!count)
            return out;

        // Targets.
        std::vector<Target> targets;
        for (EnemyFacts const& e : world.enemies)
        {
            if (!e.acting || e.onAllyOnly || !e.health ||
                PackResolver::Distance(r.centre, e.pos) - e.boundingRadius > spell.radius)
                continue;
            if (r.forbidden && r.forbidden(e))
                return out;
            if (e.crowdControlled)
                continue;
            Target t;
            t.facts = &e;
            t.free = e.victim.IsEmpty() || e.victim == self.guid;
            if (!t.free)
            {
                // The tank has not built threat on a new enemy yet.
                if (!r.paid && r.settle && ThreatRoom::Settling(world, e))
                    return out;
                ThreatEntry const* mine = ThreatView::FindEntry(e, self.guid);
                t.myThreat = (mine ? mine->threat : 0.0f) + (self.boltTarget == e.guid ? self.boltThreat : 0.0f);
                t.holderThreat = e.victimThreat;
                bool const melee = PackResolver::Distance(r.from, e.pos) - e.boundingRadius <= MELEE_REACH;
                t.factor = melee ? ThreatView::MELEE_AGGRO_FACTOR : ThreatView::RANGED_AGGRO_FACTOR;
                t.holder = world.FindMember(e.victim);
                // A holder hitting another enemy gains little here: its measured rate is a jump
                // of the last seconds, not what comes next (strat-20261107-194655). The same
                // rate as the tank's threat runway.
                if (t.holder)
                    t.holderRate = ThreatRunway::HolderRate(world, e, *t.holder);
                t.speed = SpeedTo(e, spell, r.centre, r.from);
            }
            targets.push_back(t);
            out.targets.push_back(e.guid);
        }
        if (targets.empty())
            return out;

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
            for (uint32 n = 1; n <= count; ++n)
                damage[n - 1] += std::min(spell.perTick * float(n), health);
            if (t.free)
                continue;
            for (uint32 k = 1; k <= count; ++k)
            {
                if (health <= spell.perTick * float(k))
                    break;      // the tick that kills it turns nothing
                float const mine = t.myThreat + float(k) * spell.tickThreat;
                float const holder = t.holderThreat + t.holderRate * float(k) * spell.periodSec;
                if (mine <= holder * t.factor)
                    continue;
                Turning x;
                x.tick = k;
                x.guid = t.facts->guid;
                x.arriveSec = float(k) * spell.periodSec + WalkSec(*t.facts, r.from, t.speed);
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

        // Enemies on the caster already hit it from their walk on (a channel only: pushback).
        std::vector<Hitter> onMe;
        if (spell.channel)
            for (EnemyFacts const& e : world.enemies)
                if (e.acting && !e.crowdControlled && e.health && e.victim == self.guid)
                    onMe.push_back({ &e, WalkSec(e, r.from, e.speed) });

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
            return out;
        }

        // Starting: the whole spell, its mana included.
        float b = 0.0f, c = 0.0f, l = 0.0f;
        valueOf(count, b, c, l);
        float const busySec = float(count) * spell.periodSec;
        // Not less damage per mana than the filler (no fight length guess: adds come unannounced).
        if (spell.mana > 0.0f && self.fillerManaPerSec > 0.0f &&
            damage[count - 1] / spell.mana < fillerPerSec / self.fillerManaPerSec)
            return out;
        float const extraMana = std::max(0.0f, spell.mana - self.fillerManaPerSec * busySec);
        out.mana = extraMana > 0.0f ? ManaWorth::Points(world, self, extraMana) : 0.0f;
        finish(count, b, c + out.mana, l);
        out.valid = true;
        return out;
    }
}
}
