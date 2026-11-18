#include "Commander/Commands/PlayerChat.h"
#include "Shared/BotChat.h"
#include "Chat.h"
#include "Group.h"
#include "Player.h"
#include "WorldPacket.h"
#include "WorldSession.h"

namespace Commander
{
    namespace PlayerChat
    {
        void SendAddonMessage(Player* to, char const* prefix, std::string const& body)
        {
            if (!to || !to->GetSession())
                return;
            std::string const text = std::string(prefix) + "\t" + body;
            WorldPacket data;
            // Whisper type so the 1.12 packet carries a sender.
            ChatHandler::BuildChatPacket(data, CHAT_MSG_WHISPER, text.c_str(), LANG_ADDON, CHAT_TAG_NONE,
                to->GetObjectGuid(), to->GetName());
            to->GetSession()->SendPacket(&data);
        }

        bool SayToParty(Player* speaker, std::string const& text)
        {
            return BotShared::BotChat::SayToParty(speaker, text);
        }
    }
}
