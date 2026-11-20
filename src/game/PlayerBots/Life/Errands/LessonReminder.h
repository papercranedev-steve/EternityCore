/*
 * Bot daily life (PlayerBots/Life): out in the field, a companion tells its leader it has an
 * important lesson waiting at its class trainer (user 2026-11-20: the leader may never pass a
 * trainer by). Party chat only, no popup (he cannot go right now); in town the trip question
 * lists the lessons instead (Life/Errands/TownTrip, Trip/TrainingPlanner).
 *
 * Every CHECK_MS, out of combat, the leader not in a rest area: the lessons of tiers up to
 * Lessons::REMINDED (Trip/LessonPriority.h) it has not told yet are said once; while some stay
 * unlearned it says them again after AGAIN_MS. Nothing when no capital of its faction on this
 * continent has its class trainer.
 *
 * Map thread of the bot.
 */

#ifndef MANGOS_LIFE_LESSON_REMINDER_H
#define MANGOS_LIFE_LESSON_REMINDER_H

#include "Common.h"
#include "Life/Errands/Trip/LessonPriority.h"
#include <set>
#include <string>
#include <vector>

class Player;

namespace Life
{
    // Per bot (a member of its AI). Plain data.
    struct LessonReminderState
    {
        std::set<uint32> told;      // spells said already
        uint32 nextCheckMs = 0;
        uint32 lastSaidMs = 0;
    };

    namespace LessonReminder
    {
        constexpr uint32 CHECK_MS = MINUTE * IN_MILLISECONDS;
        constexpr uint32 AGAIN_MS = 45 * MINUTE * IN_MILLISECONDS;

        void Update(Player* me, LessonReminderState& state, Player* leader, Lessons::Context const& context,
                    std::vector<std::string> const& tags);
    }
}

#endif
