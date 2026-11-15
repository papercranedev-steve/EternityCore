#include "Commander/Coordination/Duties/JudgementAssigner.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/WorldModel.h"
#include "Commander/Perception/Profiles/PaladinBuffs.h"
#include "SharedDefines.h"
#include <algorithm>

namespace Commander
{
    void JudgementAssigner::Assign(WorldModel const& world, TeamPlan& plan) const
    {
        JudgementPlan out;
        out.active = true;

        std::vector<PartyMemberFacts const*> paladins;
        for (PartyMemberFacts const& m : world.party)
            if (m.isBot && !m.isPet && m.alive && m.role != ROLE_HEALER && !m.judgementSeals.empty())
                paladins.push_back(&m);
        std::stable_sort(paladins.begin(), paladins.end(), [](PartyMemberFacts const* a, PartyMemberFacts const* b)
            { return a->guid.GetRawValue() < b->guid.GetRawValue(); });

        std::vector<uint8> taken;
        for (PartyMemberFacts const* p : paladins)
        {
            for (JudgementKind kind : { JudgementKind::Wisdom, JudgementKind::Light })
            {
                if (std::find(taken.begin(), taken.end(), uint8(kind)) != taken.end())
                    continue;
                auto const seal = std::find_if(p->judgementSeals.begin(), p->judgementSeals.end(),
                    [kind](PaladinBuffAbility const& s) { return s.kind == uint8(kind); });
                if (seal == p->judgementSeals.end())
                    continue;
                out.duties.push_back({ p->guid, seal->spellId });
                taken.push_back(uint8(kind));
                break;
            }
        }
        plan.judgements = std::move(out);
    }

    void JudgementAssigner::Clear(TeamPlan& plan) const
    {
        plan.judgements = JudgementPlan();
    }
}
