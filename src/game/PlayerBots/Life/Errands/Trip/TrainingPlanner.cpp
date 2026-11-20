#include "Life/Errands/Trip/TrainingPlanner.h"
#include "Life/Errands/TownTrip.h"
#include "Life/Errands/ErrandCommon.h"
#include "Life/Errands/NpcVisit.h"
#include "Shared/ClassTraining.h"
#include "Shared/NpcDirectory.h"
#include "Player.h"
#include "Creature.h"
#include "Map.h"

namespace Life
{
namespace Trip
{
    bool TrainingPlanner::Worth(Player* me)
    {
        uint32 cheapest = 0;
        return BotShared::ClassTraining::CheapestAvailable(me, cheapest) && me->GetMoney() >= cheapest;
    }

    void TrainingPlanner::PlanStops(Player* me, Player const* leader, TripPlanStops& plan)
    {
        if (!Worth(me))
            return;
        Map* map = me->GetMap();
        for (BotShared::NpcDirectory::ServiceNpc const& n : BotShared::NpcDirectory::Near(
                 map->GetId(), leader->GetPositionX(), leader->GetPositionY(), TownTrip::TRIP_YD, UNIT_NPC_FLAG_TRAINER))
        {
            Creature* npc = map->GetCreature(BotShared::NpcDirectory::GuidOf(n));
            if (!npc || !npc->IsAlive() || npc->IsInCombat() || me->IsHostileTo(npc) || !m_task.Wants(me, npc))
                continue;
            if (!Errands::WalkIsSafe(me, npc->GetPositionX(), npc->GetPositionY(), npc->GetPositionZ(), TownTrip::MAX_PATH_DETOUR))
                continue;
            TripStop stop;
            stop.target = npc->GetObjectGuid();
            stop.x = npc->GetPositionX();
            stop.y = npc->GetPositionY();
            stop.z = npc->GetPositionZ();
            stop.placeKey = "place.trainer";
            stop.action = std::make_shared<NpcTaskAction>(m_task);
            plan.Add(stop);
            return;
        }
    }

    char const* NpcTaskAction::Name() const
    {
        return m_task.Name();
    }

    uint32 NpcTaskAction::Do(Player* me, WorldObject* target)
    {
        Creature* npc = target->ToCreature();
        return npc && m_task.Wants(me, npc) ? m_task.Do(me, npc) : 0;
    }
}
}
