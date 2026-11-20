/*
 * Shared bot facts (PlayerBots/Shared): where to go for a quest, from the world's own data. The
 * spawns of who gives it, who takes it back, and what an objective needs: the creature or object
 * itself, or what drops an item objective (creature and chest loot, references not followed).
 * The 1.12 client marks none of this on the map (quest POIs came in 3.1); Companions/ClassQuests/
 * MirrorSync puts a flag on the leader's map from it.
 *
 * Indexed once at startup (Load), for the quests that have a mirror (Shared/QuestMirror);
 * read-only after, any thread. Spawn points: a wandering creature may be a little elsewhere.
 */

#ifndef MANGOS_BOTSHARED_QUEST_PLACES_H
#define MANGOS_BOTSHARED_QUEST_PLACES_H

#include "Common.h"
#include "Shared/QuestMirror.h"
#include <string>

class Quest;

namespace BotShared
{
    namespace QuestPlaces
    {
        // One spawn of a creature or gameobject.
        struct Place
        {
            uint32 entry = 0;
            bool gameObject = false;
            float x = 0.0f, y = 0.0f, z = 0.0f;
        };

        // Startup, after QuestMirror::BuildAll (PlayerBotMgr::Load).
        void Load();

        // The spawn nearest to x, y on map `mapId` (2d). False: none on that map.
        bool NearestGiver(Quest const* quest, uint32 mapId, float x, float y, Place& out);
        bool NearestEnder(Quest const* quest, uint32 mapId, float x, float y, Place& out);
        // For a counter of the quest's mirror: its creature or object, or what drops its item.
        // False also for a Done counter (nothing to go to).
        bool NearestFor(Quest const* quest, QuestMirror::Counter const& counter, uint32 mapId, float x, float y, Place& out);

        // The creature's or object's name in locale index `localeIdx` (-1: base).
        std::string NameOf(Place const& place, int localeIdx);
    }
}

#endif
