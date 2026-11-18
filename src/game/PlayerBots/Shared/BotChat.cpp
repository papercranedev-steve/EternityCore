#include "Shared/BotChat.h"
#include "Chat.h"
#include "Group.h"
#include "Player.h"
#include "WorldPacket.h"
#include "WorldSession.h"

namespace BotShared
{
namespace BotChat
{
    bool SayToParty(Player* speaker, std::string const& text)
    {
        Group* group = speaker ? speaker->GetGroup() : nullptr;
        if (!group || text.empty())
            return false;
        WorldPacket data;
        ChatHandler::BuildChatPacket(data, CHAT_MSG_PARTY, text.c_str(), LANG_UNIVERSAL, CHAT_TAG_NONE,
            speaker->GetObjectGuid(), speaker->GetName());
        group->BroadcastPacket(&data, false, group->GetMemberGroup(speaker->GetObjectGuid()));
        return true;
    }

    bool WhisperTo(Player* from, Player* to, std::string const& text)
    {
        if (!from || !to || !to->GetSession() || text.empty())
            return false;
        WorldPacket data;
        ChatHandler::BuildChatPacket(data, CHAT_MSG_WHISPER, text.c_str(), LANG_UNIVERSAL, CHAT_TAG_NONE,
            from->GetObjectGuid(), from->GetName());
        to->GetSession()->SendPacket(&data);
        return true;
    }

    void SystemTo(Player* to, std::string const& text)
    {
        if (!to || !to->GetSession())
            return;
        ChatHandler(to).SendSysMessage(text.c_str());
    }
}
}
