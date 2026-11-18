/*
 * Shared bot hands (PlayerBots/Shared): a bot talking to players. Party chat as a player types
 * /p, and system lines to one player (the PB...; lines the PartyBot addon reads).
 * Any thread that may send to a session (map threads, world thread).
 */

#ifndef MANGOS_BOTSHARED_BOT_CHAT_H
#define MANGOS_BOTSHARED_BOT_CHAT_H

#include "Common.h"
#include <string>

class Player;

namespace BotShared
{
    namespace BotChat
    {
        // `speaker` says `text` in party chat (raid: its subgroup). False when it has no group.
        bool SayToParty(Player* speaker, std::string const& text);

        // A system message to `to` only.
        void SystemTo(Player* to, std::string const& text);
    }
}

#endif
