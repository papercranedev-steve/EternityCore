#include "Commander/Coordination/Duties/CurseAssigner.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/WorldModel.h"
#include <algorithm>

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
            std::vector<CurseKind> taken;
            for (PartyMemberFacts const* w : warlocks)
            {
                for (CurseKind kind : order)
                {
                    if (std::find(taken.begin(), taken.end(), kind) != taken.end())
                        continue;
                    uint32 const spellId = SpellOf(w->curseSpells, kind);
                    if (!spellId)
                        continue;
                    out.duties.push_back({ w->guid, spellId, uint8(kind) });
                    taken.push_back(kind);
                    break;
                }
            }
        }
        plan.curses = std::move(out);
    }

    void CurseAssigner::Clear(TeamPlan& plan) const
    {
        plan.curses = CursePlan();
    }
}
