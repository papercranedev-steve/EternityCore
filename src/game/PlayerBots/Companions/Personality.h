/*
 * Personality tags of roster members and companions. Every tag is positive, neutral or negative;
 * a person is a mix (1-2 positive, 1-2 neutral, at most 1 negative), so nobody is only flaws.
 * Tags drive the offline simulation and the chat templates later (stages S5 / S7).
 * Design: .kiro/docs/2026-11-17-soul-companions.md §6 (workspace root).
 *
 * Tags are stored by key, so reordering or adding tags never changes who someone is.
 * Adding a tag = adding one row to the table in Personality.cpp.
 */

#ifndef MANGOS_COMPANIONS_PERSONALITY_H
#define MANGOS_COMPANIONS_PERSONALITY_H

#include "Common.h"
#include <random>
#include <string>
#include <vector>

namespace Companions
{
    enum class TagPolarity : uint8
    {
        Positive,
        Neutral,
        Negative,
    };

    struct TagDef
    {
        char const* key;            // stored in the DB, never rename
        TagPolarity polarity;
        uint8 exclusiveGroup;       // tags with the same non-zero group never come together
    };

    namespace Personality
    {
        std::vector<TagDef> const& AllTags();
        TagDef const* FindTag(std::string const& key);
        // A new mix of tags (keys). Deterministic for the same generator state.
        std::vector<std::string> Roll(std::mt19937& rng);
    }
}

#endif
