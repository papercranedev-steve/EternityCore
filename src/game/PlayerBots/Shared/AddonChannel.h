/*
 * Shared bot plumbing (PlayerBots/Shared): messages from the PartyBot addon to the server that no
 * click sends (user 2026-11-22). The 1.14 client lets an addon say a chat command only on a click
 * or a key press, unless in a group; an addon message needs neither. The addon whispers it to its
 * own player (C_ChatInfo.SendAddonMessage(prefix, text, "WHISPER", <own name>)); HermesProxy
 * forwards it as a 1.12 whisper in the addon language ("prefix\ttext"), and the core chat handler
 * (Handlers/ChatHandler.cpp, HandleChatMessageOpcode) gives it here before anything else: such a
 * whisper is neither delivered nor chat.
 *
 * Prefixes start with "PB"; each is registered once at startup (PlayerBotMgr::Load). A PB message
 * nobody registered is dropped silently.
 *
 * World thread (CMSG_MESSAGECHAT is PACKET_PROCESS_WORLD): handlers guard what map threads read.
 */

#ifndef MANGOS_BOTSHARED_ADDON_CHANNEL_H
#define MANGOS_BOTSHARED_ADDON_CHANNEL_H

#include "Common.h"
#include <string>

class Player;

namespace BotShared
{
    namespace AddonChannel
    {
        using Handler = void (*)(Player* from, std::string const& body);

        // At startup (again on a reload: replaces).
        void Register(char const* prefix, Handler handler);

        // From the chat handler: an addon-language whisper of `from` to `target` with `text`. True:
        // it was one for PlayerBots (to himself, a PB prefix), the caller drops it.
        bool Receive(Player* from, std::string const& target, std::string const& text);
    }
}

#endif
