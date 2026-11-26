#include "Commander/Coordination/Duties/SoulstoneAssigner.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/WorldModel.h"
#include "SharedDefines.h"
#include <algorithm>

namespace Commander
{
    int SoulstoneAssigner::TargetRank(PartyMemberFacts const& m)
    {
        if (m.isPet || !m.alive || m.selfRes)
            return 0;
        if (m.resurrects)
            return m.HealsTeam() ? 5 : m.isBot ? 4 : RESURRECTS_RANK;
        return m.isBot ? 1 : 2;
    }

    void SoulstoneAssigner::Assign(WorldModel const& world, TeamPlan& plan) const
    {
        SoulstonePlan out;
        out.active = true;

        std::vector<PartyMemberFacts const*> warlocks;
        std::vector<PartyMemberFacts const*> targets;
        uint32 given = 0;
        for (PartyMemberFacts const& m : world.party)
        {
            if (m.isBot && !m.isPet && m.alive && m.classId == CLASS_WARLOCK)
                warlocks.push_back(&m);
            if (!m.isPet && m.soulstoned)
                ++given;
            if (TargetRank(m) > 0)
                targets.push_back(&m);
        }
        std::stable_sort(warlocks.begin(), warlocks.end(), [](PartyMemberFacts const* a, PartyMemberFacts const* b)
        {
            if (a->soulstoneReady != b->soulstoneReady)
                return a->soulstoneReady;
            return a->guid.GetRawValue() < b->guid.GetRawValue();
        });
        std::stable_sort(targets.begin(), targets.end(), [](PartyMemberFacts const* a, PartyMemberFacts const* b)
        {
            int const ra = TargetRank(*a);
            int const rb = TargetRank(*b);
            if (ra != rb)
                return ra > rb;
            return a->guid.GetRawValue() < b->guid.GetRawValue();
        });

        size_t const wanted = warlocks.size() > given ? warlocks.size() - given : 0;
        for (size_t i = 0; i < wanted && i < targets.size(); ++i)
            out.duties.push_back({ warlocks[i]->guid, targets[i]->guid });
        plan.soulstones = std::move(out);
    }

    void SoulstoneAssigner::Clear(TeamPlan& plan) const
    {
        plan.soulstones = SoulstonePlan();
    }
}
