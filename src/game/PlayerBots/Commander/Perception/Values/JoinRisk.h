/*
 * PartyBot Commander layer 1 (eyes): what an idle creature joining the party's fight now costs,
 * in points (2026-11-15, user: "if it adds, can we hold it? then go in; a possible small add
 * must not keep the party out", stockade-20261115-012103: a lone Defias Prisoner in the
 * corridor kept mage and priest out of the tank's cell until the tank died).
 *
 *   Loss(c) = FightLoad::Loss(the fight with c and its pack mates arriving ARRIVE_SEC from now)
 *           - FightLoad::Loss(the fight as it is), at least 0
 * The fight from the facts (FightLoad::FromWorld), the party's damage Value::ActingDps (every
 * dealer acting: the member who walks in is one of them). The tank's death is in the loss
 * (FightLoad::Loss with Value::DeathPenalty of the main tank). -1: no fight to price (no tank).
 * Kept per creature for one WorldModel::version (the stance modules ask every tick).
 * Map thread of the party (reads the creature).
 */

#ifndef MANGOS_COMMANDER_JOIN_RISK_H
#define MANGOS_COMMANDER_JOIN_RISK_H

#include "Commander/Perception/WorldModel.h"
#include <vector>

class Creature;

namespace Commander
{
namespace JoinRisk
{
    float const ARRIVE_SEC = 3.0f;

    float Loss(WorldModel const& world, Creature const* creature);

    // The creature and its pack mates (WorldModel::packs): who comes when it does.
    std::vector<ObjectGuid> Joiners(WorldModel const& world, ObjectGuid creature);

    // Longest stretch a member out of reach is counted as doing nothing (its benefit of
    // walking in: Value::LostAction over min(this, the fight left)).
    float const HORIZON_SEC = 30.0f;
}
}

#endif
