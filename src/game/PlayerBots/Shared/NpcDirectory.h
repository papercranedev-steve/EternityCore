/*
 * Shared bot facts (PlayerBots/Shared): where the service NPCs of a map stand (vendors,
 * armorers, trainers ...), from the spawn table. Soul companions doc §5 (stage S5): a bot that
 * wants to walk to a vendor or trainer out of sight of the party.
 *
 * Built once per map on first use (thread safe), from spawns that are enabled and whose
 * template has npc flags. Positions are spawn points: a walking NPC may be elsewhere; the live
 * creature (Map::GetCreature(GuidOf)) is what counts once its grid is loaded.
 */

#ifndef MANGOS_BOTSHARED_NPC_DIRECTORY_H
#define MANGOS_BOTSHARED_NPC_DIRECTORY_H

#include "Common.h"
#include "ObjectGuid.h"
#include <vector>

namespace BotShared
{
    namespace NpcDirectory
    {
        struct ServiceNpc
        {
            uint32 dbGuid = 0;
            uint32 entry = 0;
            uint32 npcFlags = 0;        // CreatureInfo::npc_flags
            float x = 0.0f, y = 0.0f, z = 0.0f;
        };

        // Spawns on `mapId` with any of `npcFlagMask` within `range` (2d) of x, y; nearest first.
        std::vector<ServiceNpc> Near(uint32 mapId, float x, float y, float range, uint32 npcFlagMask);

        ObjectGuid GuidOf(ServiceNpc const& npc);
    }
}

#endif
