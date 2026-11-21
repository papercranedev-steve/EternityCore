/*
 * World population bots: shared plain types.
 */

#ifndef MANGOS_POPULATION_TYPES_H
#define MANGOS_POPULATION_TYPES_H

#include "Common.h"
#include "ObjectGuid.h"
#include "PlayerBotAI.h"    // PlayerBotAppearance
#include <memory>
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
        Town,       // residents of a town, start area or camp (TownPopulation.h)
        Herald,     // runs to a new player and asks to join him (his first companion, FirstCompanion)
    };

    enum class GearTier : uint8
    {
        Normal,     // random gear for the level (premade pre-raid set at 60)
        PreRaid,    // premade pre-raid set (60 only, otherwise Normal)
        Showcase,   // premade phase 6 set ("geared veteran"), forces level 60
    };

    // What a city or town bot does (phase 1b; towns: TownPopulation.h).
    enum class CityRole : uint8
    {
        None,       // not a city or town bot
        Stand,      // stands at an anchor (auctioneer, smith, innkeeper, ...), sometimes steps away and back
        Mover,      // runs between the place's stand spots, waits at each
        Showcase,   // geared veteran standing in view near the auction house / bank
        Crafter,    // stands near the auction house / bank / mailbox crafting in batches
        Sitter,     // sits on a chair (SpawnRequest::chairEntry) or on the ground by a fire
    };

    char const* BotKindName(BotKind kind);
    char const* GearTierName(GearTier tier);
    char const* CityRoleName(CityRole role);

    struct Waypoint
    {
        float x = 0.0f, y = 0.0f, z = 0.0f;
        float faceX = 0.0f, faceY = 0.0f;   // look at this point after arriving
    };

    // A place of a city or town a bot can go to during its day there (TownLife.h): one anchor
    // of the city or town, same index as the director's anchor (SpawnRequest::anchorIndex).
    struct PlaceAnchor
    {
        std::string type;               // PlaceAnchors.h types
        float x = 0.0f, y = 0.0f, z = 0.0f, o = 0.0f;
        bool object = false;            // a gameobject (stand all around it), else an NPC or a point
        bool exactSpot = false;         // manual scene point: stand exactly here, facing o
        uint32 chairEntry = 0;          // chair: its gameobject entry
        uint32 visitWeight = 0;         // PlaceAnchors::VisitWeight, 0: never visited
    };
    using PlaceMenu = std::shared_ptr<std::vector<PlaceAnchor> const>;

    // Gear of a roster member (Growth::Gear), applied with CombatBotBaseAI random gear limits.
    struct GearPlan
    {
        uint32 maxItemLevel = 0;
        uint32 qualityWeights[7] = {};  // poor, common, uncommon, rare, epic, legendary, artifact
        uint32 skipSlotMask = 0;        // 1 << EQUIPMENT_SLOT_*
        uint32 seed = 0;                // same member and level = same gear
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
        bool prewarm = false;       // a player is about to arrive: spawned without the PopBot.SpawnGapMs pause
        // Someone could see (x, y, z): log in out of sight at the entrance and walk there.
        bool hasEntrance = false;
        float entranceX = 0.0f, entranceY = 0.0f, entranceZ = 0.0f;
        ObjectGuid faceGuid;        // face this unit once spawned (optional)
        bool hasFacePoint = false;  // face (faceX, faceY) once spawned
        float faceX = 0.0f, faceY = 0.0f;

        // Roster member (Roster.h). Set by PopulationMgr::SpawnNow, 0 = random stranger.
        uint32 rosterId = 0;
        bool hasLook = false;
        PlayerBotAppearance look;
        bool hasGearPlan = false;   // false: GearTier rules (premade sets at 60)
        GearPlan gear;
        uint32 money = 0;           // copper

        // Field bots: the levels that fit the creatures here (0 = any), and its Commander team
        // unit (Commander/Team.h, a team of one).
        uint8 minLevel = 0, maxLevel = 0;
        uint32 unitId = 0;
        // Only these races (race mask, 0 = any): a race's start area has only that race
        // (user 2026-11-21, Settlements::StartRacesAt).
        uint32 raceMask = 0;

        // City bots
        uint32 cityZone = 0;
        CityRole role = CityRole::None;
        int32 anchorIndex = -1;         // stand / showcase / sitter: which anchor of the city or town
        std::vector<Waypoint> route;    // mover: its first spot is where it starts
        // Stand, mover, sitter: the places of the city or town for their day there (TownLife.h);
        // the role only decides where the day starts.
        PlaceMenu places;

        // Town bots: the settlement (Settlements.h id); sitter: the chair at (x, y, z), 0 = sit on the ground.
        uint32 settlementId = 0;
        uint32 chairEntry = 0;

        // The roster member's personality tags (Companions/Personality.h): how he talks
        // (Population/StrangerTalk). Set by PopulationMgr::SpawnNow.
        std::vector<std::string> tags;

        // BotKind::Herald (the player's first companion, Companions/Recruit/FirstCompanion): the
        // player he runs to, and the words for him ({giver}: who gave the player his quest).
        ObjectGuid seeks;
        std::string giverName;
    };

    // A city (zone) or a town (settlement id): the bots a director counts and retires together.
    struct PlaceKey
    {
        BotKind kind = BotKind::City;
        uint32 id = 0;

        static PlaceKey City(uint32 zone) { return { BotKind::City, zone }; }
        static PlaceKey Town(uint32 settlementId) { return { BotKind::Town, settlementId }; }

        bool Has(SpawnRequest const& r) const
        {
            if (r.kind != kind)
                return false;
            return kind == BotKind::City ? r.cityZone == id : kind == BotKind::Town ? r.settlementId == id : false;
        }
    };

    // Shared between PopulationMgr (world thread) and the bot AI. The AI clears `alive`
    // in its destructor, which runs when PlayerBotMgr erases the bot entry (world thread),
    // so the manager never touches a destroyed AI.
    struct BotHandle
    {
        bool alive = true;
        PopulationBotAI* ai = nullptr;
        // Set by the AI (map thread) when a roster member agreed to join a real player (the talk,
        // Population/StrangerTalk); the manager recruits it on its next update (world thread,
        // maps idle) and clears it. `bonded`: as a companion at once (the player's first one,
        // FirstCompanion), else as a trial partner (搭伙, recruiting doc §2.2).
        ObjectGuid recruiter;
        bool bonded = false;
        // Set by the manager (world thread, maps idle): walk to this spot out of everyone's
        // sight, then log out (the AI asks PlayerBotMgr for the removal itself).
        bool leave = false;
        float leaveX = 0.0f, leaveY = 0.0f, leaveZ = 0.0f;
        // Set by a field bot's AI (map thread): it is done here (enough fights, nothing left to
        // hunt) and would like to log out; the manager retires it (world thread, maps idle).
        bool done = false;
    };
}

#endif
