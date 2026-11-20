/*
 * Shared bot hands (PlayerBots/Shared): a bot talking to players. Party chat as a player types
 * /p, /say, whispers, text emotes (/wave), and system lines to one player (the PB...; lines the
 * PartyBot addon reads).
 * Any thread that may send to a session (map threads, world thread).
 */

#ifndef MANGOS_BOTSHARED_BOT_CHAT_H
#define MANGOS_BOTSHARED_BOT_CHAT_H

#include "Common.h"
#include <string>

class Player;
class Unit;

namespace BotShared
{
    namespace BotChat
    {
        // `speaker` says `text` in party chat (raid: its subgroup). False when it has no group.
        bool SayToParty(Player* speaker, std::string const& text);

        // `speaker` says `text` aloud (white /say: everyone in say range). False on empty text.
        bool Say(Player* speaker, std::string const& text);

        // A system message to `to` only.
        void SystemTo(Player* to, std::string const& text);

        // `from` whispers `text` to `to` (as /w; only `to` sees it). False without a session.
        bool WhisperTo(Player* from, Player* to, std::string const& text);

        // `from` does a text emote (TEXTEMOTE_*, as a player types /wave), at `target` if given:
        // its animation plays and every client in range prints the line in its own language.
        void TextEmote(Player* from, uint32 textEmote, Unit const* target = nullptr);
    }
}

#endif
