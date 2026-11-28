/*
 * Bot daily life (PlayerBots/Life), errand: gather with its gathering professions, the real
 * practice of them (user 2026-11-21; design .kiro/docs/2026-11-21-companion-professions.md
 * stage 4). Skin the corpses around once their loot is gone, open the herb and ore nodes its
 * skill can (Shared/Gathering): the spell's own cast, its skill-up and the loot it gives, taken
 * like a corpse's (Shared/LootFacts::TakeOpenLoot). Only with its tool (Skinning Knife, Mining
 * Pick: bought as supplies), and with a free bag slot.
 *
 * The leader first (user 2026-11-21): a node or corpse the leader can gather himself is left
 * to him.
 *
 * Same rules as LootRun: out of combat, nobody of the group fighting, not mounted, standing,
 * within SEARCH_YD of the bot and ANCHOR_YD of the anchor, a safe walk. A target it gathered,
 * could not reach in GIVE_UP_MS, or failed on is not tried again.
 *
 * Map thread of the bot.
 */

#ifndef MANGOS_LIFE_GATHER_RUN_H
#define MANGOS_LIFE_GATHER_RUN_H

#include "Common.h"
#include "ObjectGuid.h"
#include "Life/Errands/ErrandCommon.h"

class Player;

namespace Life
{
    // Per bot (a member of its AI). Plain data.
    struct GatherRunState
    {
        ObjectGuid target;          // a corpse to skin or a node
        uint32 startedMs = 0;
        uint32 nextMoveMs = 0;
        uint32 nextSearchMs = 0;
        bool casting = false;       // the gathering cast was started
        bool atTarget = false;      // arrived (its moment before the cast began)
        Errands::Beat beat;         // at the target before the cast, after the gathering
        Errands::DoneList done;
    };

    namespace GatherRun
    {
        constexpr float SEARCH_YD = 30.0f;
        constexpr float ANCHOR_YD = 35.0f;
        constexpr float MAX_PATH_DETOUR = 2.5f;
        constexpr float SKIN_REACH_YD = 3.5f;
        constexpr float APPROACH_YD = 2.0f;     // in front of a node (it may sit in a wall)
        constexpr uint32 SEARCH_MS = 2000;
        constexpr uint32 REPATH_MS = 2000;
        constexpr uint32 GIVE_UP_MS = 20000;
        constexpr size_t DONE_KEEP = 16;
        // Arrived: faces it, a moment, the cast; gathered: a moment, then on (Errands::Beat).
        constexpr uint32 ARRIVE_MIN_MS = 400, ARRIVE_MAX_MS = 1000;
        constexpr uint32 AFTER_MIN_MS = 800, AFTER_MAX_MS = 2000;

        // `anchor`: targets near it only (the party leader); null = near the bot.
        // True: the bot walks to a target or gathers this tick.
        bool Update(Player* me, GatherRunState& state, Player const* anchor);
        void Stop(Player* me, GatherRunState& state);
    }
}

#endif
