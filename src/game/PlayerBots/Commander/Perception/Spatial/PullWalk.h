/*
 * PartyBot Commander layer 1 (eyes): the walk to a pack about to be pulled, and the first other
 * creature it comes into on the way. One rule for everyone who asks (user 2026-11-25: the clear
 * choosing the pack in front, the pull planner choosing between walking in and opening from
 * afar, and the walk guard calling a walk-in off judged the same walk by three rules and
 * disagreed: the planner walked in, the guard called it off, the clear skipped the pack and
 * waited 30 s, the same again):
 *
 *  - the pack itself comes anyway: left out;
 *  - patrols (WorldModel::patrols) never count: where one is now says nothing about where it is
 *    when the walker gets there; the pull's patrol check times the pull around them
 *    (strat-20261125: Rockwing Screechers fly over the whole Stratholme courtyard);
 *  - only until the walk comes into the pack's own aggro range: from there the pull happens,
 *    a creature beside or behind the pack is one the pull brings (AssistPredictor), not one in
 *    between (neighbouring packs each blocked the other).
 *
 * The idle set (margin, LeavingFrom) is the caller's. Map thread: pathfinds, reads creatures.
 */

#ifndef MANGOS_COMMANDER_PULL_WALK_H
#define MANGOS_COMMANDER_PULL_WALK_H

#include "Commander/Perception/Spatial/SafeRoute.h"
#include "Commander/Perception/WorldModel.h"
#include <vector>

class Creature;
class PathInfo;
class Player;

namespace Commander
{
namespace PullWalk
{
    // The first creature of `idle` (without `pack` and `patrols`) the navmesh `path` comes into
    // before it comes into the aggro range of a member of `pack`; null = nothing in between.
    Creature const* FirstBlocker(SafeRoute::IdleCreatures const& idle, PathInfo const& path,
                                 std::vector<ObjectGuid> const& pack, std::vector<PatrolFacts> const& patrols);

    // Pathfinds from `walker` to `to`, then FirstBlocker. Null also without a complete path
    // (no way is not "something in between": the callers know the way from elsewhere).
    Creature const* Blocker(Player* walker, SafeRoute::IdleCreatures const& idle, Position3 const& to,
                            std::vector<ObjectGuid> const& pack, std::vector<PatrolFacts> const& patrols);

    // `creature` and its pack mates (WorldModel::packs), itself first.
    std::vector<ObjectGuid> PackOf(WorldModel const& world, ObjectGuid creature);
}
}

#endif
