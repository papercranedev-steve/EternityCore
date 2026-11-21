/*
 * Companions, joining the player's account (account doc .kiro/docs/2026-11-21-companion-account.md
 * §2, workspace root): at affinity 100 a companion asks his player once, in party chat and with a
 * talk page, whether he may join his account. Yes: CompanionMgr::JoinAccount (the account full:
 * he says so, make room and call him). Later / the window closed: he says it can be done any time
 * (the companions panel, the talk window: AccountService), and does not ask again.
 *
 * Addon line after a join: PBOK;<bot>;joinaccount (the panel lists him among the account's).
 */

#ifndef MANGOS_COMPANIONS_ACCOUNT_JOIN_OFFER_H
#define MANGOS_COMPANIONS_ACCOUNT_JOIN_OFFER_H

#include "Common.h"
#include "Companions/Talk/Talk.h"
#include <string>

class Player;

namespace Companions
{
namespace JoinOffer
{
    // After the gate's line ("我想一直跟着你", AffinitySources) a moment, as a person would take.
    uint32 constexpr OFFER_DELAY_MS = 6 * IN_MILLISECONDS;

    // Per party bot AI.
    struct State
    {
        uint32 dueMs = 0;       // WorldTimer ms he asks at, 0 = not due yet
    };

    // A companion `player` may ask in now (affinity 100, not in an account, his own one).
    bool MayJoin(Player const* player, Player const* bot);

    // PartyBotAI, out of combat: a companion at affinity 100 who never asked asks once.
    void Update(Player* me, Player* leader, State& state);

    // His words and the two answers (account:yes, account:later).
    Talk::Page OfferPage(Talk::Context const& ctx);

    // The player's answer (the talk page): his reply line; `joined` when he moved.
    std::string Answer(Talk::Context const& ctx, bool yes, bool& joined);
}
}

#endif
