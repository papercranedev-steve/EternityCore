/*
 * Bot daily life (PlayerBots/Life), requests: what a bot says, by phrase key and personality
 * tag. Soul companions doc §5.1 (stage S5).
 *
 * Templates live in the characters table `custom_companion_phrase` (key, tag, text); keys
 * without any row get DEFAULTS (Phrases.cpp) at startup: new lines and new tags are rows, not
 * code (edit or add rows, restart).
 *  - key: "<intent>.<moment>" ("errand.ask", "errand.go", "reward.ask" ...);
 *  - tag: a personality tag key (Companions/Personality.cpp: "frugal", "chatty" ...) or
 *    "default".
 * Pick takes a random template among those of the key matching one of the bot's tags, else
 * among the key's "default" ones. Variables are written {name} and filled by Format; unknown
 * ones stay as they are.
 *
 * Load once at startup (world thread, CompanionMgr::Load); read only afterwards (any thread).
 */

#ifndef MANGOS_LIFE_REQUEST_PHRASES_H
#define MANGOS_LIFE_REQUEST_PHRASES_H

#include "Common.h"
#include <map>
#include <string>
#include <vector>

namespace Life
{
namespace Phrases
{
    using Vars = std::map<std::string, std::string>;

    void Load();

    // Empty when the key has no template at all.
    std::string Pick(std::string const& key, std::vector<std::string> const& tags);
    std::string Format(std::string const& text, Vars const& vars);
    // Pick, then Format.
    std::string Say(std::string const& key, std::vector<std::string> const& tags, Vars const& vars);
    // A single word / label (Pick of "default" only, tags ignored); `fallback` when missing.
    std::string Word(std::string const& key, std::string const& fallback);
    // "1金20银", "35银", "8铜".
    std::string Money(uint32 copper);
}
}

#endif
