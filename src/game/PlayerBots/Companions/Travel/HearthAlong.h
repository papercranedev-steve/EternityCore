/*
 * Companions, travel: the player uses his hearthstone next to the party, and a party bot whose
 * hearthstone is bound at the same inn (Companions/Travel/HomeBind) and ready casts its own
 * with him: they arrive home together (user 2026-11-21), no "shall we wait?" (Departure) and no
 * journey after him (Life/Errands/Travel).
 *
 *  - Goes along: alive, out of combat, its hearthstone ready (Life::Travel::HearthReady), the
 *    same bind point as his (HomeBind::SAME_YD), within WITH_YD of him, not told to wait.
 *  - Told to wait (Stay: he goes to sell, a warlock of the party summons him back): it never
 *    goes along, and a cast already started stops when the order comes during it.
 *  - His cast breaks while he is still next to it: it stops its own (he stays, so does it).
 *    He is gone (teleported home): its cast goes on.
 *  - Bots that cannot go along (another bind, the hearthstone on cooldown): Departure asks
 *    only when none of the party goes along; else they follow him on their own (Travel).
 *
 * Map thread of the bot.
 */

#ifndef MANGOS_COMPANIONS_TRAVEL_HEARTH_ALONG_H
#define MANGOS_COMPANIONS_TRAVEL_HEARTH_ALONG_H

#include "Common.h"
#include <string>
#include <vector>

class Player;

namespace Companions
{
namespace HearthAlong
{
    struct State
    {
        bool casting = false;   // its hearthstone cast started with his
    };

    float constexpr WITH_YD = 50.0f;

    // He is casting his hearthstone now.
    bool LeavesByHearth(Player const* leader);
    // `me` would cast its hearthstone with him (see above). `staying`: it waits somewhere.
    bool GoesAlong(Player const* me, Player const* leader, bool staying);

    void Update(Player* me, Player* leader, State& state, bool staying, std::vector<std::string> const& tags);
}
}

#endif
