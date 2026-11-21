/*
 * What a companion takes in a trade the player opened, by affinity (Gates TradeBasics,
 * TradeAnything; design .kiro/docs/2026-11-21-companion-affinity.md §5, workspace root):
 *   below TradeBasics    nothing (the player offers anything: the trade is cancelled)
 *   TradeBasics          gold, food, drink, and gear that is an upgrade for it (Shared/GearUpgrade);
 *                        anything else in the window (worse gear, other items, the "will not be
 *                        traded" slot): the whole trade is cancelled (user 2026-11-21: simple)
 *   TradeAnything        everything
 * A trade where the player offers nothing always goes (it gives him conjured water, say).
 * The companion's own trades (Shared/Trade: asking for money, conjured supplies) do not come here.
 * Anyone not a companion takes everything (Gates::Allows).
 *
 * Map thread of the bot, while its trade window is open.
 */

#ifndef MANGOS_COMPANIONS_AFFINITY_TRADE_GATE_H
#define MANGOS_COMPANIONS_AFFINITY_TRADE_GATE_H

#include "Common.h"

class Player;

namespace Companions
{
    namespace TradeGate
    {
        enum class Refusal : uint8
        {
            None,       // it takes what is offered
            TooEarly,   // below TradeBasics and something is offered
            NotAllowed, // TradeBasics only, and something else is offered
        };

        Refusal Check(Player* me);
        // Phrase key of the line it says when it cancels.
        char const* PhraseOf(Refusal refusal);
    }
}

#endif
