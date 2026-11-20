/*
 * Bot daily life (PlayerBots/Life), town trips: the class trainer (soul companions doc §4.4).
 * Worth a trip: a new rank it can pay for (Shared/ClassTraining). The stop: its class trainer
 * nearest the leader with business (the bot's TrainerTask: Wants / Do, whose TakeLearned tells
 * the AI to rebuild its spell data).
 *
 * Map thread of the bot.
 */

#ifndef MANGOS_LIFE_TRIP_TRAINING_PLANNER_H
#define MANGOS_LIFE_TRIP_TRAINING_PLANNER_H

#include "Life/Errands/Trip/TripPlan.h"

namespace Life
{
    class INpcTask;

namespace Trip
{
    class TrainingPlanner final : public ITripPlanner
    {
    public:
        explicit TrainingPlanner(INpcTask& trainerTask) : m_task(trainerTask) {}

        char const* Name() const override { return "training"; }
        bool Worth(Player* me) override;
        void PlanStops(Player* me, Player const* leader, TripPlanStops& plan) override;

    private:
        INpcTask& m_task;
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
