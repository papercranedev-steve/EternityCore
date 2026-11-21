/*
 * What a companion did while the player was away (soul companions doc §6, stage S7): for now
 * its professions (design .kiro/docs/2026-11-21-companion-professions.md, stage 3).
 *
 * Crafting professions: skill points bought with its own money (Shared/SkillUpCost, times the
 * price factor), the lowest business (user 2026-11-21). First the money it keeps: the missing
 * supplies at list price, the repair, the important class lessons (Life/Errands/Trip/
 * LessonPriority) and the next rank of each primary profession. Of what is left it spends at
 * most half ("frugal" 30 %, "spendthrift" 80 %). At most 3 points an hour offline ("crafty" 4)
 * and 20 a day ("crafty" 30) for each profession, never past its rank's maximum (a new rank
 * needs the trainer). The professions take turns point by point.
 * Gathering professions: free and slow, a point every 8 offline hours, 3 a day ("gatherer":
 * every 4 hours, 5 a day), only with its tool (Mining Pick, Skinning Knife); the real practice
 * is gathering with the player (Life/Errands/GatherRun).
 * "A day" is the server's local calendar day; the points of the day are the caller's to keep.
 *
 * World thread, the bot in the world.
 */

#ifndef MANGOS_COMPANIONS_OFFLINE_LIFE_H
#define MANGOS_COMPANIONS_OFFLINE_LIFE_H

#include "Common.h"
#include "SharedDefines.h"
#include <map>
#include <string>
#include <vector>

class Player;

namespace Life
{
namespace Lessons
{
    struct Context;
}
}

namespace Companions
{
    namespace OfflineLife
    {
        struct Gain
        {
            uint32 skill = 0;
            uint32 points = 0;
        };

        struct Result
        {
            std::vector<Gain> gains;
            uint32 spent = 0;           // copper
            bool shortOfMoney = false;  // a crafting profession could have grown but for the money
        };

        // `today`: points each skill got today so far (skill -> points), updated.
        Result Apply(Player* bot, Life::Lessons::Context const& lessons, std::vector<std::string> const& tags,
                     uint32 offlineSecs, float priceFactor, std::map<uint32, uint32>& today);

        // The local calendar day of a unix time, as a number (year * 1000 + day of the year).
        uint32 DayOf(time_t t);

        // What it tells the party when it is back ("我不在的时候练了练专业：裁缝 +12，花了 3金"),
        // empty when there is nothing to tell. Skill names in `locale`.
        std::string Line(Result const& result, std::vector<std::string> const& tags, LocaleConstant locale);
    }
}

#endif
