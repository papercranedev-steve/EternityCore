/*
 * Bot daily life (PlayerBots/Life), town trips: the class trainer (soul companions doc §4.4).
 * Worth a trip: a lesson that is not junk (Trip/LessonPriority.h), or junk it can pay for;
 * the leader may give the money (Shortfall, TownTrip's "pay"). The stop: in a capital its class
 * trainer anywhere in the city (user 2026-11-20: far trainers were never passed by), elsewhere
 * the one nearest the leader within TownTrip::TRIP_YD. The question lists every lesson by tier
 * and, when the money is short, what the important ones and all of them cost (Notes).
 *
 * Map thread of the bot.
 */

#ifndef MANGOS_LIFE_TRIP_TRAINING_PLANNER_H
#define MANGOS_LIFE_TRIP_TRAINING_PLANNER_H

#include "Life/Errands/Trip/TripPlan.h"

namespace Life
{
    class INpcTask;
    class TrainerTask;

namespace Trip
{
    class TrainingPlanner final : public ITripPlanner
    {
    public:
        explicit TrainingPlanner(TrainerTask& trainerTask) : m_task(trainerTask) {}

        char const* Name() const override { return "training"; }
        bool Worth(Player* me) override;
        void PlanStops(Player* me, Player const* leader, TripPlanStops& plan) override;
        void Notes(Player* me, Player const* reader, std::vector<Requests::Ask::Note>& out) override;
        uint32 Shortfall(Player* me) override;
        char const* PurposeKey() const override { return "errand.why.train"; }

    private:
        TrainerTask& m_task;
    };

    // Runs an NPC visit task (Life/Errands/NpcVisit.h) as a trip stop.
    class NpcTaskAction final : public IStopAction
    {
    public:
        explicit NpcTaskAction(INpcTask& task) : m_task(task) {}
        char const* Name() const override;
        uint32 Do(Player* me, WorldObject* target) override;

    private:
        INpcTask& m_task;
    };
}
}

#endif
