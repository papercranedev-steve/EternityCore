#include "Commander/Perception/AssistPredictor.h"
#include "Creature.h"
#include "CreatureGroups.h"
#include "Map.h"
#include "World.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "CellImpl.h"
#include <algorithm>
#include <deque>
#include <set>

namespace Commander
{
namespace AssistPredictor
{
    namespace
    {
        // Members of an aggro-together group join without the assist flag, so they call too.
        void AddGroupMembers(Creature* c, std::set<ObjectGuid>& seen, std::deque<Creature*>& callers,
                             std::vector<ObjectGuid>& out)
        {
            CreatureGroup* group = c->GetCreatureGroup();
            if (!group || !group->HasGroupFlag(OPTION_AGGRO_TOGETHER))
                return;
            for (auto const& itr : group->GetMembers())
            {
                Creature* m = c->GetMap()->GetCreature(itr.first);
                if (!m || !m->IsAlive() || m->IsInCombat() || seen.count(m->GetObjectGuid()))
                    continue;
                seen.insert(m->GetObjectGuid());
                out.push_back(m->GetObjectGuid());
                callers.push_back(m);
            }
            // the leader is not always in its own member list
            if (Creature* leader = c->GetMap()->GetCreature(group->GetOriginalLeaderGuid()))
            {
                if (leader->IsAlive() && !leader->IsInCombat() && !seen.count(leader->GetObjectGuid()))
                {
                    seen.insert(leader->GetObjectGuid());
                    out.push_back(leader->GetObjectGuid());
                    callers.push_back(leader);
                }
            }
        }
    }

    std::vector<ObjectGuid> Predict(Creature* target, Unit* puller)
    {
        std::vector<ObjectGuid> out;
        if (!target || !puller)
            return out;

        float const radius = sWorld.getConfig(CONFIG_FLOAT_CREATURE_FAMILY_ASSISTANCE_RADIUS);
        std::set<ObjectGuid> seen;
        std::deque<Creature*> callers;

        seen.insert(target->GetObjectGuid());
        out.push_back(target->GetObjectGuid());
        callers.push_back(target);
        AddGroupMembers(target, seen, callers, out);

        // Bounded: every creature is added once.
        while (!callers.empty())
        {
            Creature* caller = callers.front();
            callers.pop_front();
            if (radius <= 0.0f)
                continue;

            std::list<Creature*> helpers;
            MaNGOS::AnyAssistCreatureInRangeCheck check(caller, puller, radius);
            MaNGOS::CreatureListSearcher<MaNGOS::AnyAssistCreatureInRangeCheck> searcher(helpers, check);
            Cell::VisitGridObjects(caller, searcher, radius);

            for (Creature* h : helpers)
            {
                if (seen.count(h->GetObjectGuid()))
                    continue;
                seen.insert(h->GetObjectGuid());
                out.push_back(h->GetObjectGuid());
                // helper itself does not call again, but its aggro-together group joins and calls
                AddGroupMembers(h, seen, callers, out);
            }
        }
        return out;
    }
}
}
