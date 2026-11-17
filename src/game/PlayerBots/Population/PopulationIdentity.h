/*
 * World population bots: who a bot is (name, race, class, level).
 *
 * Names are English. Built-in syllable lists per race plus "player style" compound names;
 * extra names can be added to the world table `custom_popbot_name` (sql/custom/popbot.sql).
 * World thread only.
 */

#ifndef MANGOS_POPULATION_IDENTITY_H
#define MANGOS_POPULATION_IDENTITY_H

#include "Common.h"
#include <random>
#include <set>
#include <string>
#include <vector>

namespace Population
{
    class Identity
    {
    public:
        // Reads custom_popbot_name. Missing table = only built-in names.
        void Load();
        uint32 GetTableNameCount() const;

        // Free name (not an existing character, not in `reserved`), already normalized.
        // Empty string if nothing free was found.
        // rng: draw from this generator (roster generation from the world seed); nullptr = urand.
        std::string MakeName(uint8 race, std::set<std::string> const& reserved, std::mt19937* rng = nullptr) const;

        // Random playable race for the team (ALLIANCE / HORDE).
        static uint8 RandomRace(uint32 team, std::mt19937* rng = nullptr);
        // Random class valid for the race. 0 if none.
        static uint8 RandomClass(uint8 race, std::mt19937* rng = nullptr);
        // Uniform in [lo, hi] from rng, or urand when rng is nullptr.
        static uint32 Roll(std::mt19937* rng, uint32 lo, uint32 hi);
        static bool IsValidCombo(uint8 race, uint8 classId);
        // City crowd: about half at 60, the rest 10-59.
        static uint8 RandomCityLevel();

    private:
        std::string RandomBuiltInName(uint8 race, std::mt19937* rng) const;

        std::vector<std::string> m_anyRace;             // race 0 in the table
        std::vector<std::vector<std::string>> m_byRace; // index = race id
    };
}

#endif
