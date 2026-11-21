/*
 * Bot daily life (PlayerBots/Life), town trips: the profession trainers (design
 * .kiro/docs/2026-11-21-companion-professions.md, stage 2). Ranks below Artisan and new
 * recipes of its primary professions (Shared/Professions::Available).
 *
 * The lowest business (user 2026-11-21): it spends only what is left above the money the class
 * trainer wants for the important lessons (Trip/LessonPriority), and its stops come after every
 * other stop of the trip (TripStop::last), so the supplies are bought first. No money is asked
 * of the leader for it (no Shortfall).
 * Worth a trip on its own: a new rank it can pay. Recipes go along when the trip happens.
 * The stops: in a capital a trainer of each such profession anywhere in the city, elsewhere the
 * one nearest the leader within TownTrip::TRIP_YD; a trainer teaching the rank is preferred.
 *
 * Map thread of the bot.
 */

#ifndef MANGOS_LIFE_TRIP_PROFESSION_PLANNER_H
#define MANGOS_LIFE_TRIP_PROFESSION_PLANNER_H

#include "Life/Errands/Trip/TripPlan.h"
#include "Life/Errands/Trip/LessonPriority.h"
#include <set>

namespace Life
{
namespace Trip
{
    class ProfessionPlanner final : public ITripPlanner
    {
    public:
        // `lessons`: the class trainer's (TrainerTask), for the money kept for it; null: all
        // class lessons are kept for.
        explicit ProfessionPlanner(Lessons::Context const* lessons) : m_lessons(lessons) {}

        char const* Name() const override { return "profession"; }
        bool Worth(Player* me) override;
        void PlanStops(Player* me, Player const* leader, TripPlanStops& plan) override;
        void Notes(Player* me, Player const* reader, std::vector<Requests::Ask::Note>& out) override;
        char const* PurposeKey() const override { return "errand.why.profession"; }

    private:
        Lessons::Context const* m_lessons;
        std::set<uint32> m_planned;     // skills with a stop on the last plan (Notes)
    };
}
}

#endif
