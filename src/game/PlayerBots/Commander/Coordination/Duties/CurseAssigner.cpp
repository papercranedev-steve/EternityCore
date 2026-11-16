#include "Commander/Coordination/Duties/CurseAssigner.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/Values/CurseValue.h"
#include "Commander/Perception/WorldModel.h"
#include "SpellEntry.h"
#include "SpellMgr.h"
#include <algorithm>
#include <utility>

namespace Commander
{
    namespace
    {
        uint32 SpellOf(std::vector<CurseAbility> const& spells, CurseKind kind)
        {
            for (CurseAbility const& s : spells)
                if (s.kind == uint8(kind))
                    return s.spellId;
            return 0;
        }

        // Its own curse of this kind on the enemy (the one it keeps: no flipping on small changes).
        bool OwnCurseOn(EnemyFacts const& enemy, ObjectGuid warlock, CurseKind kind)
        {
            for (CurseOnFacts const& c : enemy.curses)
                if (c.kind == uint8(kind) && c.caster == warlock)
                    return true;
            return false;
        }

        // Benefit - cost of this warlock's curse of `kind` on `enemy` (CurseValue).
        float Net(WorldModel const& world, PartyMemberFacts const& warlock, EnemyFacts const& enemy, uint32 spellId)
        {
            SpellEntry const* spell = sSpellMgr.GetSpellEntry(spellId);
            if (!spell)
                return 0.0f;
            CurseEffect const curse = WarlockCurses::EffectOf(spell);
            return CurseValue::Benefit(world, enemy, curse) - CurseValue::Cost(world, warlock, enemy, curse);
        }
    }

    std::vector<CurseKind> CurseAssigner::Order(WorldModel const& world)
    {
        std::vector<CurseKind> order = WarlockCurses::TieOrder();
        auto countOf = [&world](CurseKind kind)
        {
            uint32 n = 0;
            for (PartyMemberFacts const& m : world.party)
                if (!m.isPet && WarlockCurses::Favoured(m.classId, m.role) == kind)
                    ++n;
            return n;
        };
        std::vector<uint32> counts;
        for (CurseKind kind : order)
            counts.push_back(countOf(kind));

        std::vector<size_t> index(order.size());
        for (size_t i = 0; i < index.size(); ++i)
            index[i] = i;
        // Stable: equal counts keep the tie order.
        std::stable_sort(index.begin(), index.end(), [&counts](size_t a, size_t b) { return counts[a] > counts[b]; });

        std::vector<CurseKind> out;
        for (size_t i : index)
            if (counts[i] > 0)
                out.push_back(order[i]);
        return out;
    }

    void CurseAssigner::Assign(WorldModel const& world, TeamPlan& plan) const
    {
        CursePlan out;
        out.active = true;

        std::vector<PartyMemberFacts const*> warlocks;
        for (PartyMemberFacts const& m : world.party)
            if (m.isBot && !m.isPet && !m.curseSpells.empty())
                warlocks.push_back(&m);
        std::stable_sort(warlocks.begin(), warlocks.end(), [](PartyMemberFacts const* a, PartyMemberFacts const* b)
            { return a->guid.GetRawValue() < b->guid.GetRawValue(); });

        if (!warlocks.empty())
        {
            std::vector<CurseKind> const order = Order(world);
            // (enemy, kind) already given: the same curse from two warlocks does not stack.
            std::vector<std::pair<ObjectGuid, CurseKind>> taken;
            auto isTaken = [&taken](ObjectGuid enemy, CurseKind kind)
            {
                return std::find(taken.begin(), taken.end(), std::make_pair(enemy, kind)) != taken.end();
            };

            for (PartyMemberFacts const* w : warlocks)
            {
                EnemyFacts const* enemy = world.FindEnemy(w->victim);
                ObjectGuid const key = enemy ? enemy->guid : ObjectGuid();

                CurseKind best = CurseKind::None;
                uint32 bestSpell = 0;
                float bestNet = 0.0f;
                bool bestHeld = false;
                CurseKind fallback = CurseKind::None;
                uint32 fallbackSpell = 0;
                for (CurseKind kind : order)
                {
                    if (isTaken(key, kind))
                        continue;
                    uint32 const spellId = SpellOf(w->curseSpells, kind);
                    if (!spellId)
                        continue;
                    if (fallback == CurseKind::None)
                    {
                        fallback = kind;
                        fallbackSpell = spellId;
                    }
                    if (!enemy)
                        break;
                    float const net = Net(world, *w, *enemy, spellId);
                    if (net <= 0.0f)
                        continue;
                    // The curse it already keeps there stays while it pays; otherwise the best net,
                    // ties in the count order.
                    bool const held = OwnCurseOn(*enemy, w->guid, kind);
                    if (bestHeld)
                        continue;
                    if (held || best == CurseKind::None || net > bestNet)
                    {
                        best = kind;
                        bestSpell = spellId;
                        bestNet = net;
                        bestHeld = held;
                    }
                }
                // No victim yet, or nothing pays on it: the most wanted one (CurseBehavior
                // proposes it, the arbiter drops it while it does not pay).
                if (best == CurseKind::None)
                {
                    best = fallback;
                    bestSpell = fallbackSpell;
                }
                if (best == CurseKind::None)
                    continue;
                out.duties.push_back({ w->guid, bestSpell, uint8(best) });
                taken.emplace_back(key, best);
            }
        }
        plan.curses = std::move(out);
    }

    void CurseAssigner::Clear(TeamPlan& plan) const
    {
        plan.curses = CursePlan();
    }
}
