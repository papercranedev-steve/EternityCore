/*
 * PartyBot Commander layer 3: the stances' last way in (2026-11-15, user: a possible small add
 * must not keep the party out; stockade-20261115-012103).
 *
 * The stance found no spot in the fight, out of its target's reach, and the first cell it tried
 * failed only on an idle creature (SpotRejects::blockerUnit). Walk there anyway when it pays:
 *   benefit = Value::LostAction(member, min(JoinRisk::HORIZON_SEC, the fight left)): it does
 *             nothing until then otherwise;
 *   cost    = JoinRisk::Loss (that creature and its pack join the fight) + the walk.
 * The walk is checked on the hard line with that creature and its pack left out
 * (JoinRisk::Joiners) and ArrivingAt the fight; the cell must still be clear of every other
 * idle creature. Tier Assigned, SetValue(benefit, cost). One check per failed path (the
 * stances keep a failed path a while: SightCache::PathFailed).
 */

#ifndef MANGOS_COMMANDER_JOIN_GAMBLE_H
#define MANGOS_COMMANDER_JOIN_GAMBLE_H

#include "Commander/Behaviors/IBehavior.h"
#include "Commander/Behaviors/Movement/SpotRejects.h"

class Player;

namespace Commander
{
namespace JoinGamble
{
    float const MAX_PATH_DETOUR = 1.8f;
    float const PATH_FACTOR = 1.2f;
    uint32 const HOLD_EXTRA_MS = 500;

    // True: a proposal was added to `out`.
    bool Propose(Player* me, WorldModel const& world, PartyMemberFacts const& self, SpotRejects const& why,
                 Position3 const& fightAt, char const* source, std::vector<Proposal>& out);
}
}

#endif
