#include "Commander/Perception/AssistPredictor.h"
#include "Commander/Perception/AggroRange.h"
#include "Creature.h"
#include "CreatureGroups.h"
#include "Map.h"
#include "World.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "CellImpl.h"
#include <algorithm>
#include <cmath>
#include <deque>
#include <list>
#include <set>

namespace Commander
{
namespace AssistPredictor
{
    namespace
    {
        void Add(Creature* c, JoinReason reason, std::set<ObjectGuid>& seen, std::vector<Joiner>& out)
        {
            seen.insert(c->GetObjectGuid());
            Joiner j;
            j.guid = c->GetObjectGuid();
            j.reason = reason;
            out.push_back(j);
        }

        // Members of an aggro-together group join without the assist flag, so they call too.
        void AddGroupMembers(Creature* c, std::set<ObjectGuid>& seen, std::deque<Creature*>& callers,
                             std::vector<Joiner>& out)
        {
            CreatureGroup* group = c->GetCreatureGroup();
            if (!group || !group->HasGroupFlag(OPTION_AGGRO_TOGETHER))
                return;
            for (auto const& itr : group->GetMembers())
            {
                Creature* m = c->GetMap()->GetCreature(itr.first);
                if (!m || !m->IsAlive() || m->IsInCombat() || seen.count(m->GetObjectGuid()))
                    continue;
                Add(m, JoinReason::AggroTogether, seen, out);
                callers.push_back(m);
            }
            // the leader is not always in its own member list
            if (Creature* leader = c->GetMap()->GetCreature(group->GetOriginalLeaderGuid()))
            {
                if (leader->IsAlive() && !leader->IsInCombat() && !seen.count(leader->GetObjectGuid()))
                {
                    Add(leader, JoinReason::AggroTogether, seen, out);
                    callers.push_back(leader);
                }
            }
        }

        // Idle creature that would attack `puller` standing at `at` on its own (AggroRange.h).
        class AggroRangeCheck
        {
        public:
            AggroRangeCheck(Unit const* puller, Position3 const& at) : m_puller(puller), m_at(at) {}
            bool operator()(Creature* c)
            {
                return AggroRange::IsIdleAggressor(c, m_puller) &&
                    AggroRange::Covers(c, AggroRange::Reach(c, m_puller, AGGRO_RANGE_MARGIN), m_at);
            }

        private:
            Unit const* m_puller;
            Position3 m_at;
        };
    }

    namespace
    {
        // Every caller calls assistance once (Creature::CallAssistance); helpers do not call
        // again, their aggro-together groups do. Bounded: every creature is added once.
        void CallAssistance(std::deque<Creature*>& callers, Unit* enemy, std::set<ObjectGuid>& seen, std::vector<Joiner>& out)
        {
            float const radius = sWorld.getConfig(CONFIG_FLOAT_CREATURE_FAMILY_ASSISTANCE_RADIUS);
            while (!callers.empty())
            {
                Creature* caller = callers.front();
                callers.pop_front();
                if (radius <= 0.0f)
                    continue;

                std::list<Creature*> helpers;
                MaNGOS::AnyAssistCreatureInRangeCheck check(caller, enemy, radius);
                MaNGOS::CreatureListSearcher<MaNGOS::AnyAssistCreatureInRangeCheck> searcher(helpers, check);
                Cell::VisitGridObjects(caller, searcher, radius);

                for (Creature* h : helpers)
                {
                    if (seen.count(h->GetObjectGuid()))
                        continue;
                    Add(h, JoinReason::Assist, seen, out);
                    AddGroupMembers(h, seen, callers, out);
                }
            }
        }
    }

    std::vector<Joiner> PredictDetailed(Creature* target, Unit* puller, Position3 const* standAt)
    {
        std::vector<Joiner> out;
        if (!target || !puller)
            return out;

        std::set<ObjectGuid> seen;
        std::deque<Creature*> callers;

        Add(target, JoinReason::Target, seen, out);
        callers.push_back(target);
        AddGroupMembers(target, seen, callers, out);

        // Aggro-range joiners start attacking by themselves, so they call assistance too.
        if (standAt)
        {
            std::list<Creature*> nearby;
            AggroRangeCheck check(puller, *standAt);
            MaNGOS::CreatureListSearcher<AggroRangeCheck> searcher(nearby, check);
            // Grid search is centered on an object: the target, widened by its distance to the stand point.
            float const dx = target->GetPositionX() - standAt->x;
            float const dy = target->GetPositionY() - standAt->y;
            float const searchRadius = AGGRO_SEARCH_RADIUS + std::sqrt(dx * dx + dy * dy);
            Cell::VisitGridObjects(target, searcher, searchRadius);
            for (Creature* c : nearby)
            {
                if (seen.count(c->GetObjectGuid()))
                    continue;
                Add(c, JoinReason::AggroRange, seen, out);
                callers.push_back(c);
                AddGroupMembers(c, seen, callers, out);
            }
        }

        CallAssistance(callers, puller, seen, out);
        return out;
    }

    std::vector<Joiner> Expand(std::vector<Creature*> const& seeds, Unit* enemy, std::vector<ObjectGuid> const& exclude)
    {
        std::vector<Joiner> out;
        if (!enemy)
            return out;
        std::set<ObjectGuid> seen(exclude.begin(), exclude.end());
        std::deque<Creature*> callers;
        for (Creature* c : seeds)
        {
            if (!c || seen.count(c->GetObjectGuid()))
                continue;
            Add(c, JoinReason::FleeCall, seen, out);
            callers.push_back(c);
            AddGroupMembers(c, seen, callers, out);
        }
        CallAssistance(callers, enemy, seen, out);
        return out;
    }

    std::vector<ObjectGuid> Predict(Creature* target, Unit* puller)
    {
        return Guids(PredictDetailed(target, puller, nullptr));
    }

    std::vector<ObjectGuid> Guids(std::vector<Joiner> const& joiners)
    {
        std::vector<ObjectGuid> out;
        out.reserve(joiners.size());
        for (Joiner const& j : joiners)
            out.push_back(j.guid);
        return out;
    }

    char const* ReasonName(JoinReason reason)
    {
        switch (reason)
        {
            case JoinReason::Target:        return "target";
            case JoinReason::Assist:        return "assist";
            case JoinReason::AggroTogether: return "group";
            case JoinReason::AggroRange:    return "aggro range";
            case JoinReason::FleeCall:      return "flee call";
        }
        return "?";
    }
}
}
