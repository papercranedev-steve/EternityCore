#include "Commander/Coordination/Situation/Pull/Camps.h"
#include "Commander/Perception/Spatial/PackResolver.h"
#include <algorithm>

namespace Commander
{
namespace Camps
{
    namespace
    {
        float Distance(Position3 const& a, Position3 const& b)
        {
            return PackResolver::Distance(a, b);
        }

        bool Has(std::vector<uint32> const& entries, uint32 entry)
        {
            return std::find(entries.begin(), entries.end(), entry) != entries.end();
        }

        bool Near(CampMemory const& camp, Position3 const& p)
        {
            return std::any_of(camp.spots.begin(), camp.spots.end(), [&p](Position3 const& s) { return Distance(s, p) <= CAMP_YD; });
        }
    }

    int32 Find(std::vector<CampMemory> const& camps, PackFacts const& pack)
    {
        for (size_t i = 0; i < camps.size(); ++i)
        {
            CampMemory const& camp = camps[i];
            bool const kind = std::any_of(pack.members.begin(), pack.members.end(),
                [&camp](PackCreature const& c) { return Has(camp.entries, c.entry); });
            if (kind && Near(camp, pack.center))
                return int32(i);
        }
        return -1;
    }

    void Grow(CampMemory& camp, PackFacts const& pack)
    {
        if (camp.spots.size() >= MAX_CAMP_SPOTS)
            return;
        for (Position3 const& s : camp.spots)
            if (Distance(s, pack.center) < CAMP_SPOT_STEP_YD)
                return;
        camp.spots.push_back(pack.center);
    }

    void Forget(std::vector<CampMemory>& camps, Position3 const& leaderPos, uint32 nowMs)
    {
        camps.erase(std::remove_if(camps.begin(), camps.end(), [&](CampMemory const& c)
        {
            return int32(c.untilMs - nowMs) <= 0 ||
                std::none_of(c.spots.begin(), c.spots.end(), [&](Position3 const& s) { return Distance(s, leaderPos) <= CAMP_FORGET_YD; });
        }), camps.end());
    }

    void RememberFight(std::vector<CampMemory>& camps, LeaderEngage const& fight, uint32 nowMs)
    {
        if (fight.entries.empty())
            return;
        for (CampMemory& c : camps)
            if (c.byPlayer && Has(c.entries, fight.entries.front()) && Near(c, fight.pos))
            {
                c.untilMs = nowMs + STARTED_MEMORY_MS;
                if (c.spots.size() < MAX_CAMP_SPOTS)
                    c.spots.push_back(fight.pos);
                return;
            }
        CampMemory fresh;
        fresh.entries = fight.entries;
        fresh.spots.push_back(fight.pos);
        fresh.pull = true;
        fresh.byPlayer = true;
        fresh.untilMs = nowMs + STARTED_MEMORY_MS;
        camps.insert(camps.begin(), std::move(fresh));
    }

    void RememberAnswer(std::vector<CampMemory>& camps, std::vector<uint32> const& entries, Position3 const& where,
                        bool clear, uint32 nowMs)
    {
        CampMemory camp;
        camp.entries = entries;
        camp.spots.push_back(where);
        camp.pull = clear;
        camp.untilMs = nowMs + ANSWER_MEMORY_MS;
        camps.insert(camps.begin(), std::move(camp));
    }

    std::vector<uint32> EntriesOf(PackFacts const& pack)
    {
        std::vector<uint32> entries;
        for (PackCreature const& c : pack.members)
            if (!Has(entries, c.entry))
                entries.push_back(c.entry);
        return entries;
    }
}
}
