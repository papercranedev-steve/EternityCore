#include "Commander/Coordination/Situation/Pull/BossWaits.h"
#include "Commander/Coordination/Situation/ClearPlanner.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/WorldModel.h"
#include <algorithm>

namespace Commander
{
namespace BossWaits
{
    namespace
    {
        bool Overlaps(std::vector<ObjectGuid> const& a, std::vector<ObjectGuid> const& b)
        {
            return std::any_of(a.begin(), a.end(), [&b](ObjectGuid g)
            {
                return std::find(b.begin(), b.end(), g) != b.end();
            });
        }
    }

    bool Holds(AutoClearPlan& autoClear, PackFacts const& pack, ObjectGuid selection)
    {
        std::vector<ObjectGuid> const key = ClearPlanner::PackKey(pack);
        auto const it = std::find_if(autoClear.bossWaits.begin(), autoClear.bossWaits.end(),
            [&key](BossWait const& w) { return Overlaps(w.pack, key); });
        if (it == autoClear.bossWaits.end())
            return false;
        bool const selected = !selection.IsEmpty() && std::any_of(pack.members.begin(), pack.members.end(),
            [selection](PackCreature const& c) { return c.guid == selection; });
        if (!selected && pack.ApproachYd() > it->approachYd - CLOSER_YD)
            return true;
        autoClear.bossWaits.erase(it);
        return false;
    }

    void Remember(AutoClearPlan& autoClear, std::vector<ObjectGuid> const& pack, float approachYd)
    {
        Forget(autoClear, pack);
        BossWait w;
        w.pack = pack;
        w.approachYd = approachYd;
        autoClear.bossWaits.push_back(std::move(w));
    }

    void Forget(AutoClearPlan& autoClear, std::vector<ObjectGuid> const& pack)
    {
        autoClear.bossWaits.erase(std::remove_if(autoClear.bossWaits.begin(), autoClear.bossWaits.end(),
            [&pack](BossWait const& w) { return Overlaps(w.pack, pack); }), autoClear.bossWaits.end());
    }
}
}
