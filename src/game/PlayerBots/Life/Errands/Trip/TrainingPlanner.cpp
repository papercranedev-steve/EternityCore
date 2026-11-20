#include "Life/Errands/Trip/TrainingPlanner.h"
#include "Life/Errands/Trip/LessonPriority.h"
#include "Life/Errands/TownTrip.h"
#include "Life/Errands/ErrandCommon.h"
#include "Life/Errands/MerchantTasks.h"
#include "Life/Errands/NpcVisit.h"
#include "Life/Requests/Phrases.h"
#include "Shared/Capitals.h"
#include "Shared/NpcDirectory.h"
#include "Player.h"
#include "Creature.h"
#include "Map.h"
#include "ObjectMgr.h"
#include "WorldSession.h"

namespace Life
{
namespace Trip
{
    namespace
    {
        Lessons::Context const& ContextOf(TrainerTask const& task)
        {
            static Lessons::Context const none;
            return task.LessonContext() ? *task.LessonContext() : none;
        }

        bool IsOwnClassTrainer(Player const* me, CreatureInfo const* info)
        {
            return info && (info->npc_flags & UNIT_NPC_FLAG_TRAINER) && info->trainer_type == TRAINER_TYPE_CLASS &&
                   info->trainer_class == me->GetClass();
        }

        void AddStop(TrainerTask& task, BotShared::NpcDirectory::ServiceNpc const& n, TripPlanStops& plan, bool farSpawn)
        {
            TripStop stop;
            stop.farSpawn = farSpawn;
            stop.target = BotShared::NpcDirectory::GuidOf(n);
            stop.x = n.x;
            stop.y = n.y;
            stop.z = n.z;
            stop.placeKey = "place.trainer";
            stop.action = std::make_shared<NpcTaskAction>(task);
            plan.Add(stop);
        }

        // In a capital of its faction: its class trainer anywhere in the city, from the spawn
        // table (a far trainer's grid may not be loaded yet: the walk loads it).
        bool PlanInCapital(Player* me, Player const* leader, TrainerTask& task, TripPlanStops& plan)
        {
            BotShared::Capitals::Capital const* capital = BotShared::Capitals::OfZone(leader->GetZoneId());
            Map* map = me->GetMap();
            if (!capital || capital->mapId != map->GetId())
                return false;
            if (capital->team != me->GetTeam())
                return true;    // (an enemy city: no trainer of ours)
            for (BotShared::NpcDirectory::ServiceNpc const& n : BotShared::NpcDirectory::Near(
                     map->GetId(), leader->GetPositionX(), leader->GetPositionY(), 2.0f * capital->radius, UNIT_NPC_FLAG_TRAINER))
            {
                float const dx = n.x - capital->x;
                float const dy = n.y - capital->y;
                if (dx * dx + dy * dy > capital->radius * capital->radius)
                    continue;
                if (!IsOwnClassTrainer(me, sObjectMgr.GetCreatureTemplate(n.entry)))
                    continue;
                if (Creature* npc = map->GetCreature(BotShared::NpcDirectory::GuidOf(n)))
                    if (!npc->IsAlive() || npc->IsInCombat() || me->IsHostileTo(npc))
                        continue;
                AddStop(task, n, plan, true);
                return true;
            }
            return true;
        }
    }

    bool TrainingPlanner::Worth(Player* me)
    {
        for (Lessons::Listed const& lesson : Lessons::Available(me, ContextOf(m_task)))
            if (lesson.tier != Lessons::Tier::Junk || lesson.cost <= me->GetMoney())
                return true;
        return false;
    }

    void TrainingPlanner::PlanStops(Player* me, Player const* leader, TripPlanStops& plan)
    {
        if (!Worth(me) || PlanInCapital(me, leader, m_task, plan))
            return;
        Map* map = me->GetMap();
        for (BotShared::NpcDirectory::ServiceNpc const& n : BotShared::NpcDirectory::Near(
                 map->GetId(), leader->GetPositionX(), leader->GetPositionY(), TownTrip::TRIP_YD, UNIT_NPC_FLAG_TRAINER))
        {
            Creature* npc = map->GetCreature(BotShared::NpcDirectory::GuidOf(n));
            if (!npc || !npc->IsAlive() || npc->IsInCombat() || me->IsHostileTo(npc) ||
                !IsOwnClassTrainer(me, npc->GetCreatureInfo()))
                continue;
            if (!Errands::WalkIsSafe(me, npc->GetPositionX(), npc->GetPositionY(), npc->GetPositionZ(), TownTrip::MAX_PATH_DETOUR))
                continue;
            AddStop(m_task, n, plan, false);
            return;
        }
    }

    void TrainingPlanner::Notes(Player* me, Player const* reader, std::vector<Requests::Ask::Note>& out)
    {
        std::vector<Lessons::Listed> const lessons = Lessons::Available(me, ContextOf(m_task));
        LocaleConstant const locale = reader && reader->GetSession() ? reader->GetSession()->GetSessionDbcLocale() : LOCALE_enUS;
        for (Lessons::Listed const& lesson : lessons)
            out.push_back({ uint8(lesson.tier), Lessons::NameOf(lesson.spellId, locale) });
        if (Shortfall(me))
        {
            Phrases::Vars vars;
            vars["important"] = Phrases::Money(Lessons::CostUpTo(lessons, Lessons::IMPORTANT));
            vars["all"] = Phrases::Money(Lessons::CostUpTo(lessons, Lessons::Tier::Junk));
            vars["have"] = Phrases::Money(me->GetMoney());
            out.push_back({ 0, Phrases::Say("training.money", {}, vars) });
        }
    }

    uint32 TrainingPlanner::Shortfall(Player* me)
    {
        uint32 const all = Lessons::CostUpTo(Lessons::Available(me, ContextOf(m_task)), Lessons::Tier::Junk);
        return all > me->GetMoney() ? all - me->GetMoney() : 0;
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
