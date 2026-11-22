/*
 * Companions, joining the player's account (account doc .kiro/docs/2026-11-21-companion-account.md
 * §2, workspace root): at affinity 100 a companion asks his player once, in party chat and with a
 * talk page, whether he may join his account. Yes: he asks for a name, as a rite (§2.1, Account/
 * Naming): keep his own (CompanionMgr::JoinAccount now) or the addon's name box (the window
 * "name", whose `.partybot joinaccount <guid> <name>` joins him). The account full: he says so at
 * the yes, make room and call him. Later / the window closed: he says it can be done any time
 * (the companions panel, the talk window: AccountService), and does not ask again.
 *
 * Addon line after a join: PBOK;<old name>;joinaccount[;<new name>] (the panel lists him among
 * the account's).
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

    // The player chose to name him: he goes down on one knee in front of him while the name box
    // is open (user 2026-11-22), and rises once he joined, at the box's 取消 (account:later),
    // when either fights, the player walks off (Talk::TALK_YD) or after this long.
    uint32 constexpr KNEEL_MAX_MS = 2 * MINUTE * IN_MILLISECONDS;

    // Per party bot AI.
    struct State
    {
        uint32 dueMs = 0;       // WorldTimer ms he asks at, 0 = not due yet
        uint32 kneelUntilMs = 0;    // kneeling for his name until then, 0 = not
    };

    // A companion `player` may ask in now (affinity 100, not in an account, his own one).
    bool MayJoin(Player const* player, Player const* bot);

    // PartyBotAI, out of combat: a companion at affinity 100 who never asked asks once. True
    // while he kneels for his name: he holds still (no stand-up, no following).
    bool Update(Player* me, Player* leader, State& state);

    // His words and the two answers (account:yes, account:later).
    Talk::Page OfferPage(Talk::Context const& ctx);

    // The answers of his pages (AccountService, `arg` after "account:"): "" his offer page,
    // yes the naming page (or the account is full), keep he joins with his name, name the
    // addon's name box, later not now.
    Talk::Outcome Choose(Talk::Context const& ctx, std::string const& arg);

    // He joined by `.partybot joinaccount` (the menu, the name box): his line to the party,
    // `oldName` his name before (the same: he kept it; empty: no name was asked).
    std::string JoinedLine(Talk::Context const& ctx, std::string const& oldName);
}
}

#endif
