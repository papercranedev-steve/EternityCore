/*
 * PartyBot Commander: chat packets to and from players, shared by the outputs that talk to the
 * user (ThreatFeed, BotSpeech, the dev console's u:addonmsg / u:partysay).
 *
 * Addon messages: a whisper from the player to himself with LANG_ADDON and the text
 * "<prefix>\t<body>", which HermesProxy turns into CHAT_MSG_ADDON when the 1.14 addon
 * registered the prefix (ChatPkt::CheckAddonPrefix). The whole text (prefix, tab, body) must
 * stay within ADDON_MESSAGE_MAX bytes.
 */

#ifndef MANGOS_COMMANDER_PLAYER_CHAT_H
#define MANGOS_COMMANDER_PLAYER_CHAT_H

#include "Common.h"
#include <string>

class Player;

namespace Commander
{
    namespace PlayerChat
    {
        size_t const ADDON_MESSAGE_MAX = 255;

        // Any thread that may send to the player's session (map threads, world thread).
        void SendAddonMessage(Player* to, char const* prefix, std::string const& body);

        // `speaker` says `text` in party chat (raid: its subgroup), like a player typing /p.
        // False when it has no group.
        bool SayToParty(Player* speaker, std::string const& text);
    }
}

#endif
