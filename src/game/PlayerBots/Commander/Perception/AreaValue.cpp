#include "Commander/Perception/AreaValue.h"
#include "Commander/Perception/ManaWorth.h"
#include "Commander/Perception/PackResolver.h"
#include "Commander/Perception/ThreatRoom.h"
#include "Commander/Perception/ThreatView.h"
#include "Commander/Perception/ValueModel.h"
#include "SpellMgr.h"
#include <algorithm>

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

        // Speed of `e` running at the caster once the spell's slow is on it (an enemy already
        // slowed keeps its speed: the chill is not counted twice).
        float SpeedUnder(EnemyFacts const& e, float slowPct)
        {
            if (slowPct <= 0.0f || e.slowed)
                return e.speed;
            return e.speed * (1.0f - slowPct);
        }
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
                // of the last seconds, not what comes next (strat-20261107-194655).
                if (t.holder && t.holder->victim == e.guid)
                    if (ThreatEntry const* h = ThreatView::FindEntry(e, t.holder->guid))
                        t.holderRate = h->threatPerSec * TANK_GAIN_SHARE;
                t.speed = SpeedUnder(e, spell.slowPct);
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

        AggroTurnCost::Options options = r.turn;
        options.at = &r.from;
        float const fillerPerSec = self.fillerDps;
        float const fillerManaPerSec = r.paid ? self.fillerManaPerSec : 0.0f;
        std::vector<AggroTurnCost::Turn> group;
        auto const valueOf = [&](uint32 n, float& benefit, float& cost, float& turnLoss)
        {
            group.clear();
            turnLoss = 0.0f;
            for (Turning const& x : turning)
            {
                if (x.tick > n)
                    continue;
                if (x.turn.enemy)
                    group.push_back(x.turn);
                else
                    turnLoss += x.hitLoss;
            }
            if (!group.empty())
                turnLoss += AggroTurnCost::OfGroup(world, group, self, options);
            float const sec = float(n) * spell.periodSec;
            benefit = Value::LostAction(world, self, damage[n - 1] / fillerPerSec) +
                      (fillerManaPerSec > 0.0f ? ManaWorth::Points(world, self, fillerManaPerSec * sec) : 0.0f);
            cost = Value::LostAction(world, self, sec) + turnLoss;
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
