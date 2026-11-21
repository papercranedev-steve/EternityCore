/*
 * A companion's professions before it met the player (user 2026-11-21): we decide them, the
 * player does not. Two primary professions as a pair that suits its armor (Tailoring +
 * Enchanting for cloth, Skinning + Leatherworking for leather and mail, Mining + Blacksmithing
 * for plate ...; the "gatherer" tag prefers two gathering skills), plus First Aid, Cooking and
 * Fishing. A level-1 recruit has them too.
 *
 * Skill: one tier behind a player of its level: level x 5 (at most 300) minus 75, +-15, at
 * least 1, at most 225 (Expert: a level-60 recruit can go for Artisan, with the player). The
 * ranks that hold it, whatever the level; the trainer recipes up to it, half of the recent ones
 * (within 25 points) left for the player to take it training; the Apprentice basics (learned at
 * skill 1) always known. A gathering profession comes with its basic tool (Mining Pick, Skinning
 * Knife) when it has none.
 *
 * Decided once at recruiting (or, for a companion recruited before this, when first online),
 * seeded by the character: the same companion always gets the same past.
 * Design: .kiro/docs/2026-11-21-companion-professions.md §2 (workspace root).
 */

#ifndef MANGOS_COMPANIONS_PAST_PROFESSIONS_H
#define MANGOS_COMPANIONS_PAST_PROFESSIONS_H

#include "Common.h"
#include <string>
#include <vector>

class Player;

namespace Companions
{
    namespace PastProfessions
    {
        struct Result
        {
            uint32 first = 0;           // skill lines of the primary professions
            uint32 second = 0;
            uint32 firstValue = 0;
            uint32 secondValue = 0;
            uint32 recipes = 0;         // learned, every profession
        };

        // Forgets whatever professions it had (a city crafter's), then gives it its own.
        // World thread, the bot in the world.
        Result Decide(Player* bot, std::vector<std::string> const& tags);

        // Skill value a recruit of `level` has (see above), roll 0..1.
        uint32 RecruitValue(uint32 level, float roll);
    }
}

#endif
