/*
 * Companions, the name he takes into the player's account (account doc
 * .kiro/docs/2026-11-21-companion-account.md §2.1, workspace root): saying yes to his offer, the
 * player may give him a new name, once (user 2026-11-22: no renaming later). He asks it as a
 * rite (JoinOffer::NamingPage); the addon's name box sends `.partybot joinaccount <guid> <name>`
 * and CompanionMgr::JoinAccount checks the name before he moves, so a refused name changes
 * nothing.
 *
 * The rules are the ones of creating a character (normalizePlayerName, ObjectMgr::CheckPlayerName,
 * reserved names), and no one may have it: characters, and the people of the world who are not
 * all in `characters` (roster members, strangers, population bots: PopulationMgr::IsNameInUse).
 *
 * World thread.
 */

#ifndef MANGOS_COMPANIONS_ACCOUNT_NAMING_H
#define MANGOS_COMPANIONS_ACCOUNT_NAMING_H

#include "Common.h"
#include <string>

class Player;

namespace Companions
{
namespace Naming
{
    // `name` is normalized (first letter upper case ...). Empty: it may be his (also his own
    // name: nothing to rename). Else why not: name_empty, name_short, name_long, name_repeat
    // (three same letters in a row), name_chars (letters of mixed or unknown alphabets),
    // name_reserved (not allowed), name_taken.
    std::string Check(uint32 charGuid, std::string& name);

    // He is called `newName` from now on: the character, the name cache, him in the world and
    // in the player's group (also offline there), his roster member (memberId, 0 = none), and
    // every client asks his name again. `owner`: the player whose group he may be in.
    void Rename(Player* owner, uint32 charGuid, uint32 memberId, std::string const& oldName, std::string const& newName);
}
}

#endif
