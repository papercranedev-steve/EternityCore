/*
 * A companion's past (what it did before it met the player) is rolled once, the same for the
 * same character and question: 0..1 from the character guid and a key (splitmix64).
 * Keys: class quest chains use their chain id (a quest id); professions (PastProfessions) set
 * the high bit, so the two never meet.
 */

#ifndef MANGOS_COMPANIONS_PAST_ROLL_H
#define MANGOS_COMPANIONS_PAST_ROLL_H

#include "Common.h"

namespace Companions
{
    inline float PastRoll(uint32 charGuid, uint32 key)
    {
        uint64 x = (uint64(charGuid) << 32) | key;
        x += 0x9E3779B97F4A7C15ULL;
        x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
        x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
        x ^= x >> 31;
        return float(x >> 40) / float(1ULL << 24);
    }
}

#endif
