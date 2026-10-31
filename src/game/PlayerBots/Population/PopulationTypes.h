/*
 * World population bots: shared plain types.
 */

#ifndef MANGOS_POPULATION_TYPES_H
#define MANGOS_POPULATION_TYPES_H

#include "Common.h"
#include "ObjectGuid.h"
#include <string>

namespace Population
{
    class PopulationBotAI;

    enum class BotKind : uint8
    {
        Test,       // .popbot spawn
        City,       // phase 1b
        Field,      // phase 1c
    };

    enum class GearTier : uint8
    {
        Normal,     // random gear for the level (premade pre-raid set at 60)
        PreRaid,    // premade pre-raid set (60 only, otherwise Normal)
        Showcase,   // premade phase 6 set ("geared veteran"), forces level 60
    };

    char const* BotKindName(BotKind kind);
    char const* GearTierName(GearTier tier);

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
