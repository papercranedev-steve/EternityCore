/*
 * Companions, talking to one (design .kiro/docs/2026-11-21-companion-talk.md §2, workspace
 * root): a service is one kind of thing the player can ask him in the talk window ("让我看看你
 * 的包", "在这儿等我", "能帮我配点药水吗？"). The window shows what the services offer now; the
 * addon only draws it.
 *
 * A new service = one class (Offer + Choose) in Talk/Services/ and one line in Services.cpp.
 * Offer gives nothing when the service cannot be had now (class, profession, affinity gate,
 * combat, materials ...): the window never shows grey options, like a real NPC's gossip.
 * Choose runs only for an option Offer gives at that moment (Talk::Choose checks), so it does
 * not check the gate again.
 *
 * Map thread of the bot (chat commands).
 */

#ifndef MANGOS_COMPANIONS_TALK_SERVICE_H
#define MANGOS_COMPANIONS_TALK_SERVICE_H

#include "Common.h"
#include <memory>
#include <string>
#include <vector>

class Player;
class PartyBotAI;

namespace Companions
{
namespace Talk
{
    // The native gossip icons (the addon draws Interface\GossipFrame\<Name>GossipIcon).
    enum class Icon : uint8
    {
        Chat,
        Vendor,
        Taxi,
        Trainer,
        Binder,
        Banker,
    };
    char const* IconKey(Icon icon);

    struct Context
    {
        Player* player = nullptr;
        Player* bot = nullptr;
        PartyBotAI* ai = nullptr;
        std::vector<std::string> const* tags = nullptr;   // its personality (Phrases)
        uint32 affinity = 0;                              // 0..100 (any party bot: 0)
        bool companion = false;                           // recruited (not a plain party bot)
    };

    struct Option
    {
        std::string id;         // "<service key>" or "<service key>:<argument>"
        Icon icon = Icon::Chat;
        std::string text;       // what the player says (his words)
        uint32 price = 0;       // copper it costs the player, 0 = nothing
    };

    // What happens after the player picked an option.
    struct Outcome
    {
        enum class Kind : uint8
        {
            Close,      // the window closes
            Page,       // a new page: `text` and `options`
            Open,       // the window closes and the addon opens `window` (with `arg`)
        };
        Kind kind = Kind::Close;
        std::string reply;      // what he says aloud first (may be empty)
        std::string text;       // Page: his words on the new page
        std::vector<Option> options;
        std::string window;     // Open: "bags", "craft"
        std::string arg;
    };

    class Service
    {
    public:
        virtual ~Service() = default;
        // The first part of its option ids.
        virtual char const* Key() const = 0;
        // Its options now, appended to `out` (none: not offered).
        virtual void Offer(Context const& ctx, std::vector<Option>& out) const = 0;
        // `arg`: the part of the option id after "<key>:" (empty without one).
        virtual Outcome Choose(Context const& ctx, std::string const& arg) const = 0;
    };

    // Every service, in the order the window lists them (Services.cpp).
    std::vector<std::unique_ptr<Service>> const& Services();
}
}

#endif
