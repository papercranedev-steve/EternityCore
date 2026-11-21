/*
 * Shared bot core (PlayerBots/Shared): which language our own text is in (bot speech, the
 * PartyBot addon's server lines, mirror quest texts). Chinese and English (design
 * .kiro/docs/2026-11-21-i18n.md, workspace root).
 *
 * A player's language: what the PartyBot addon set for his account (`.partybot language zh|en`,
 * characters table `custom_account_language`), else his client's locale (zhCN / zhTW: Chinese),
 * else English. Text from game data (item, spell, quest, area names) keeps following the
 * client's locale: that is not ours.
 * Who reads a bot's words: the human it talks to (Of), or the humans of its party (OfParty: the
 * leader first). Nobody known (a lone population bot): PartyBot.Language in mangosd.conf.
 *
 * Any thread (one lock around the account map; set from the world thread).
 */

#ifndef MANGOS_BOTSHARED_I18N_H
#define MANGOS_BOTSHARED_I18N_H

#include "Common.h"
#include <string>

class Player;

namespace BotShared
{
namespace I18n
{
    enum class Lang : uint8 { Zh = 0, En = 1 };
    uint8 constexpr COUNT = 2;

    // "zh" / "en": the code in tables, commands and the addon.
    char const* Code(Lang lang);
    bool Parse(std::string const& code, Lang& out);

    void LoadConfig();
    // The accounts' choices (world thread, startup).
    void Load();

    // Nobody known to read it (PartyBot.Language, default English).
    Lang Default();
    // The language of a client locale (zhCN / zhTW: Chinese).
    Lang OfLocale(LocaleConstant locale);
    // Text for `reader`. A bot reads nothing itself: its party's (OfParty).
    Lang Of(Player const* reader);
    // Text said to `member`'s party (a bot speaking): its human leader's, else its first human's,
    // else Default.
    Lang OfParty(Player const* member);

    // The account's choice (the addon's switch); saved.
    void Set(uint32 accountId, Lang lang);
}
}

#endif
