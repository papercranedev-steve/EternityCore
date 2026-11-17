/*
 * PartyBot Commander layer 1 (eyes): the hostile creatures of this dungeon still alive, from
 * the `creature` table (the push mode's goals, PushPlanner).
 *
 * A dungeon is not loaded at once: grids load around players. So per spawn:
 *  - a creature object on the map: it counts while it is alive and not fighting; its position
 *    (a patrol walked off its spawn point), hostility and type from the live object;
 *  - no object but its grid loaded: not spawned now (a pool, an event, a linked spawn): left out;
 *  - grid not loaded: alive unless its respawn time (MapPersistentState) is still ahead;
 *    hostility and type from creature_template.
 * Left out: critters, totems, guards and civilians, creatures not hostile to `viewer` (neutral
 * ones never start a fight), disabled or dead-by-default spawns.
 *
 * The spawn list of a map is read once from ObjectMgr and kept (any thread, locked); the
 * alive check runs on the map thread of `map`.
 */

#ifndef MANGOS_COMMANDER_SPAWN_TARGETS_H
#define MANGOS_COMMANDER_SPAWN_TARGETS_H

#include "Commander/Perception/WorldModel.h"
#include <vector>

class Map;
class Player;

namespace Commander
{
namespace SpawnTargets
{
    struct Target
    {
        uint32 dbGuid = 0;
        uint32 entry = 0;
        ObjectGuid guid;
        Position3 pos;          // the live creature's, else the spawn point
        bool boss = false;      // CreatureProfile::boss or a world boss rank
        bool loaded = false;    // a live object was seen
    };

    std::vector<Target> Alive(Map* map, Player const* viewer);
}
}

#endif
