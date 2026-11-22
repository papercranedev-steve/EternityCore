/*
 * Bot daily life (PlayerBots/Life), errand: a bot that hunts alone has nothing to fight within
 * reach and goes looking, as a player does, instead of standing where its last fight ended.
 *
 * Update (bot update, out of combat, while the caller's hunt has found nothing):
 *   - the nearest creature it could fight (ProwlRules: alive, attackable, not fighting, nobody
 *     else's loot, not elite, a critter or a pet, in the level band, the quest's creature when
 *     one is given) beyond `nearYd` (nearer ones are the hunt's to judge) and within SEARCH_YD
 *     (of the area's middle when an area is given): it runs to a point STOP_SHORT_YD short of
 *     it, with a safe walk (Commander/Perception/Spatial/SafeRoute: no other idle pack on the
 *     way). One it reached, could not reach in GIVE_UP_MS or had no safe walk to is not tried
 *     again;
 *   - none: every WANDER_MIN_MS..WANDER_MAX_MS it walks to a random point of the area (a quest's
 *     creatures, or around where it hunts), on about the same level.
 *
 * Who prowls, and when the hunt has found nothing, are the caller's (Population/PopulationBotAI:
 * field bots, the Commander's HuntPlan). Map thread of the bot.
 */

#ifndef MANGOS_LIFE_PROWL_H
#define MANGOS_LIFE_PROWL_H

#include "Common.h"
#include "ObjectGuid.h"
#include "Life/Errands/ErrandCommon.h"

class Player;

namespace Life
{
    // What the bot looks for, and where.
    struct ProwlRules
    {
        uint32 minLevel = 0;
        uint32 maxLevel = 0;
        uint32 preyEntry = 0;           // 0: any creature
        float nearYd = 0.0f;            // nearer creatures are the hunt's (it found no fight with them)
        float areaX = 0.0f, areaY = 0.0f, areaZ = 0.0f;
        float areaRadius = 0.0f;        // the wander circle; creatures within SEARCH_YD of its middle
    };

    // Per bot (a member of its AI). Plain data.
    struct ProwlState
    {
        ObjectGuid prey;                // walking towards it
        bool wandering = false;         // walking to a random point of the area
        uint32 startedMs = 0;
        uint32 nextSearchMs = 0;
        uint32 nextWanderMs = 0;
        Errands::DoneList done;
    };

    namespace Prowl
    {
        constexpr float SEARCH_YD = 120.0f;
        constexpr float STOP_SHORT_YD = 25.0f;
        constexpr float MAX_PATH_DETOUR = 2.5f;
        constexpr uint32 CANDIDATES = 4;        // safe walks checked per search (pathfinding)
        constexpr uint32 SEARCH_MS = 3000;
        constexpr uint32 GIVE_UP_MS = 40000;
        constexpr uint32 WANDER_MIN_MS = 8000;
        constexpr uint32 WANDER_MAX_MS = 20000;
        constexpr uint32 WANDER_TRIES = 8;
        constexpr float WANDER_MAX_DZ = 8.0f;
        constexpr size_t DONE_KEEP = 16;
        // A creature it walked to or gave up on counts again after this (it may have moved).
        constexpr uint32 FORGET_MS = 120000;

        // True: the bot walks this tick (the caller does nothing else with its movement).
        bool Update(Player* me, ProwlState& state, ProwlRules const& rules);

        // The hunt found a fight (its movement is the Commander's now), or the bot no longer
        // prowls. `stopWalk`: also end the walk this errand started.
        void Stop(Player* me, ProwlState& state, bool stopWalk);
    }
}

#endif
