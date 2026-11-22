#include "Shared/AddonChannel.h"
#include "ObjectMgr.h"
#include "Player.h"
#include <unordered_map>

namespace BotShared
{
namespace AddonChannel
{
    namespace
    {
        std::unordered_map<std::string, Handler>& Handlers()
        {
            static std::unordered_map<std::string, Handler> handlers;
            return handlers;
        }

        // "Name" or "Name-Realm" -> "Name" as the server writes it (empty: not a name).
        std::string PlainName(std::string name)
        {
            size_t const dash = name.find('-');
            if (dash != std::string::npos)
                name.resize(dash);
            if (!normalizePlayerName(name))
                name.clear();
            return name;
        }
    }

    void Register(char const* prefix, Handler handler)
    {
        Handlers()[prefix] = handler;
    }

    bool Receive(Player* from, std::string const& target, std::string const& text)
    {
        if (!from || text.size() < 3 || text[0] != 'P' || text[1] != 'B')
            return false;
        size_t const tab = text.find('\t');
        if (tab == std::string::npos)
            return false;
        std::string const name = PlainName(target);
        if (name.empty() || name != from->GetName())
            return false;

        auto it = Handlers().find(text.substr(0, tab));
        if (it != Handlers().end() && it->second)
            it->second(from, text.substr(tab + 1));
        return true;
    }
}
}
