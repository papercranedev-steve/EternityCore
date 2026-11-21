/*
 * Shared bot core (PlayerBots/Shared): our own text by key, language and personality tag: what a
 * bot says, and the words of the PartyBot addon's server lines (labels, trait names). Soul
 * companions doc §5.1; languages: I18n.h; design .kiro/docs/2026-11-21-i18n.md.
 *
 * The text lives in one JSON file per language, PlayerBots/i18n/<code>.json (installed next to
 * mangosd, folder PartyBot.I18nDir), read at startup (`.partybot language reload` reads them
 * again). A key's value is
 *  - a string: one line;
 *  - an array of strings: lines picked at random;
 *  - an object, personality tag (Companions/Personality.cpp: "frugal", "chatty" ...) or
 *    "default" -> a string or an array.
 * Keys starting with "//" are notes for translators (what the variables are), not text.
 *  - key: "<intent>.<moment>" ("errand.ask", "errand.go", "reward.ask" ...).
 * Pick takes a random line of the key in the language among those of the bot's tags, else
 * among the key's "default" ones; a key the language does not have is taken from the other
 * language (a line not translated yet). Variables are written {name} and filled by Format;
 * unknown ones stay as they are. A key no language has is logged once.
 * Checks (missing keys, variables that differ between languages): contrib/i18n/i18n.mjs.
 *
 * Any thread (the tables are swapped under a lock on reload).
 */

#ifndef MANGOS_BOTSHARED_PHRASES_H
#define MANGOS_BOTSHARED_PHRASES_H

#include "Common.h"
#include "Shared/I18n.h"
#include <map>
#include <string>
#include <vector>

namespace BotShared
{
namespace Phrases
{
    using Lang = I18n::Lang;
    using Vars = std::map<std::string, std::string>;

    void LoadConfig();
    // Reads the language files; false when one could not be read (logged).
    bool Load();

    // Empty when the key has no line in any language.
    std::string Pick(Lang lang, std::string const& key, std::vector<std::string> const& tags);
    std::string Format(std::string const& text, Vars const& vars);
    // Pick, then Format.
    std::string Say(Lang lang, std::string const& key, std::vector<std::string> const& tags, Vars const& vars);
    // A single word / label (the "default" lines, tags ignored).
    std::string Word(Lang lang, std::string const& key);
    // A key that may be missing (one per spell, per skill ...): `fallback` then, nothing logged.
    std::string Word(Lang lang, std::string const& key, std::string const& fallback);
    // A label with variables (Word, then Format).
    std::string Text(Lang lang, std::string const& key, Vars const& vars);
    // "1金20银" / "1g 20s".
    std::string Money(Lang lang, uint32 copper);
}
}

#endif
