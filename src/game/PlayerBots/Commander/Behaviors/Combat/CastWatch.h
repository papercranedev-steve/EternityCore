/*
 * PartyBot Commander: second look at a healer bot's heal while it is being cast
 * (progress.md, open item "checks in the middle of a cast").
 *
 * The arbiter runs after PartyBotAI::UpdateAI returns for a bot that is casting, so nothing
 * reconsidered a cast once it started. PartyBotAI::UpdateAI calls CastWatch::Update every
 * frame, before its bot update throttle: the cast is tracked every frame (cheap pointer
 * compare), the checks below run at most every CHECK_INTERVAL_MS.
 *
 * Only for healer bots, Commander on, and only for a
 * single-target direct heal with a cast time (not channelled, not area, not chained).
 * Resurrections and any other cast have no heal effect and are never touched. Targets below
 * ThreatBehavior::HEAL_IGNORE_THREAT_PCT are never cancelled.
 *
 *  Overheal - the target's health went up since the cast started (another heal, a HoT): the
 *             cast has more than OVERHEAL_MIN_LEFT_MS left, the health the target will miss
 *             when it lands (missing now + incoming melee x time left) is below
 *             OVERHEAL_KEEP_FRACTION of the heal, and the missing health dropped below
 *             OVERHEAL_DROP_FRACTION of what it was at cast start. Heals that also put a HoT
 *             on the target (Regrowth) are not cancelled for overheal.
 *             The "dropped since start" rule keeps a heal the rotation chose on purpose for a
 *             small injury from being cancelled straight away and recast in a loop.
 *  Snipe    - a steady healer (ClassKit::HealsSteadily, paladin; user 2026-11-16) starts heals
 *             before the hits (HealPlan.h): within SNIPE_LEFT_MS of landing, a heal of which
 *             less than SNIPE_KEEP_FRACTION would be effective is cancelled (no mana spent).
 *  Aggro    - (`threat` module on) the heal's threat (ThreatView::PredictHealThreat on live threat refs, headroom
 *             from the commander's WorldModel snapshot) makes an enemy turn on the healer.
 *             Enemies not in the snapshot yet are ignored (cancelling is the expensive choice).
 *             At most one aggro cancel per target every AGGRO_CANCEL_COOLDOWN_MS: if the
 *             rotation casts the same heal again it lands.
 *
 * Map thread only (reads live units and the commander of the bot's own map).
 */

#ifndef MANGOS_COMMANDER_CAST_WATCH_H
#define MANGOS_COMMANDER_CAST_WATCH_H

#include "Common.h"
#include "ObjectGuid.h"

class PartyBotAI;
class Spell;
class Unit;
class SpellEntry;

namespace Commander
{
    // Per bot (PartyBotAI member). Plain data.
    struct CastWatchState
    {
        Spell const* spell = nullptr;   // the watched cast, compared only (never dereferenced)
        uint32 spellId = 0;
        ObjectGuid target;
        bool eligible = false;          // single-target direct heal with a cast time
        bool periodic = false;          // also applies a periodic heal (no overheal cancel)
        float startMissing = 0.0f;      // target's missing health when the cast was first seen
        uint32 lastCheckMs = 0;
        ObjectGuid aggroCancelTarget;   // last aggro cancel
        uint32 aggroCancelMs = 0;
    };

    namespace CastWatch
    {
        constexpr uint32 CHECK_INTERVAL_MS = 200;
        constexpr uint32 OVERHEAL_MIN_LEFT_MS = 300;
        constexpr float OVERHEAL_KEEP_FRACTION = 0.25f;     // x estimated heal
        constexpr float OVERHEAL_DROP_FRACTION = 0.5f;      // x missing health at cast start
        constexpr uint32 AGGRO_CANCEL_COOLDOWN_MS = 5000;
        // Snipe (steady healers): looked at in the last SNIPE_LEFT_MS of the cast (at least two
        // checks), cancelled when less than SNIPE_KEEP_FRACTION of the heal would be effective.
        constexpr uint32 SNIPE_LEFT_MS = 500;
        constexpr float SNIPE_KEEP_FRACTION = 0.5f;

        // Every frame from PartyBotAI::UpdateAI. True = the cast was cancelled.
        bool Update(PartyBotAI* bot, CastWatchState& state);
    }
}

#endif
