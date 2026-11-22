/*
 * Companions, talking to one like to an NPC (design .kiro/docs/2026-11-21-companion-talk.md
 * §1, workspace root). The player walks up to him (TALK_YD) and opens the talk window: he turns
 * to the player, makes the talking gesture and greets him by personality, affinity and how he
 * is (low on mana, in a capital); below come the options of the services (TalkService.h). A
 * picked option: he answers aloud, then does it.
 *
 * Any party bot of the player's group can be talked to (a plain one has every gate open, like
 * Affinity/Gates). Later "pick a line" becomes "say a line"; what comes after stays.
 *
 * Map thread of the bot (chat commands).
 */

#ifndef MANGOS_COMPANIONS_TALK_H
#define MANGOS_COMPANIONS_TALK_H

#include "Companions/Talk/TalkService.h"

namespace Companions
{
namespace Talk
{
    float constexpr TALK_YD = 10.0f;

    enum class Refusal : uint8
    {
        None,
        NotInGroup,     // not of the player's group
        Far,            // farther than TALK_YD
        Busy,           // he or the player fights (`line`: what he says)
        Dead,
    };

    // Whether the player may talk to him now. `line`: his words for Busy.
    Refusal Check(Player* player, Player* bot, std::string& line);

    // The context for services (his tags, affinity).
    Context ContextOf(Player* player, Player* bot);

    struct Page
    {
        std::string text;
        std::vector<Option> options;
    };

    // Opens the window: he faces the player and talks; the greeting and what he offers now.
    Page Open(Context const& ctx);

    // Every option the services offer now.
    std::vector<Option> Offers(Context const& ctx);

    // The player picked `optionId`. False: it is not offered now (the window was old), `out`
    // then holds his words why ("talk.gone").
    bool Choose(Context const& ctx, std::string const& optionId, Outcome& out);

    // His line for `key` (Life/Requests/Phrases) with {leader} = the player, {me} = his own
    // name; other variables stay for the caller to fill (Phrases::Format); empty when missing.
    std::string Line(Context const& ctx, std::string const& key);

    // The page to the player's addon (PBTALK BEGIN, OPT, END; TalkCommands.cpp): an answer, or
    // one he opens himself (a trial partner offering to stay, a stranger asking why he was
    // invited).
    void SendPage(Player* to, Player* bot, Page const& page);
}
}

#endif
