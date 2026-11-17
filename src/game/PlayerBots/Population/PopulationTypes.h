/*
 * World population bots: shared plain types.
 */

#ifndef MANGOS_POPULATION_TYPES_H
#define MANGOS_POPULATION_TYPES_H

#include "Common.h"
#include "ObjectGuid.h"
#include "PlayerBotAI.h"    // PlayerBotAppearance
#include <string>
#include <vector>

namespace Population
{
    class PopulationBotAI;

    enum class BotKind : uint8
    {
        Test,       // .popbot spawn
        City,       // filled by the city director while a player is in the city
        Field,      // phase 1c
    };

    enum class GearTier : uint8
    {
        Normal,     // random gear for the level (premade pre-raid set at 60)
        PreRaid,    // premade pre-raid set (60 only, otherwise Normal)
        Showcase,   // premade phase 6 set ("geared veteran"), forces level 60
    };

    // What a city bot does (phase 1b).
    enum class CityRole : uint8
    {
        None,       // not a city bot
        Stand,      // stands at an auctioneer / banker / mailbox, sometimes steps away and back
        Mover,      // runs between the city's stand spots, waits at each
        Showcase,   // geared veteran standing in view near the auction house / bank
        Crafter,    // stands near the auction house / bank / mailbox crafting in batches
    };

    char const* BotKindName(BotKind kind);
    char const* GearTierName(GearTier tier);
    char const* CityRoleName(CityRole role);

    struct Waypoint
    {
        float x = 0.0f, y = 0.0f, z = 0.0f;
        float faceX = 0.0f, faceY = 0.0f;   // look at this point after arriving
    };

    struct SpawnRequest
    {
        BotKind kind = BotKind::Test;
        uint8 race = 0;             // 0 = random for the team
        uint8 classId = 0;          // 0 = random valid for the race
        uint8 level = 0;            // 0 = random
        uint32 team = 0;            // ALLIANCE / HORDE, used when race == 0
        GearTier tier = GearTier::Normal;
        uint32 mapId = 0;
        float x = 0.0f, y = 0.0f, z = 0.0f, o = 0.0f;
        uint32 lifetimeMs = 0;      // 0 = random from PopBot.Lifetime*
        ObjectGuid faceGuid;        // face this unit once spawned (optional)
        bool hasFacePoint = false;  // face (faceX, faceY) once spawned
        float faceX = 0.0f, faceY = 0.0f;

        // Roster member (Roster.h). Set by PopulationMgr::SpawnNow, 0 = random stranger.
        uint32 rosterId = 0;
        bool hasLook = false;
        PlayerBotAppearance look;

        // City bots
        uint32 cityZone = 0;
        CityRole role = CityRole::None;
        int32 anchorIndex = -1;         // stand / showcase: which anchor of the city
        std::vector<Waypoint> route;    // mover: spots to run between
    };

    // Shared between PopulationMgr (world thread) and the bot AI. The AI clears `alive`
    // in its destructor, which runs when PlayerBotMgr erases the bot entry (world thread),
    // so the manager never touches a destroyed AI.
    struct BotHandle
    {
        bool alive = true;
        PopulationBotAI* ai = nullptr;
    };
}

#endif
