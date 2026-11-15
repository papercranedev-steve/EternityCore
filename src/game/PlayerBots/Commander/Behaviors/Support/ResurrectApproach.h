/*
 * PartyBot Commander layer 3 (cerebellum) with its hands: a bot with a resurrection walks to a
 * dead member it cannot resurrect from where it stands (out of range or out of sight), as a
 * player does (user, 2026-11-15: the priest only resurrected bodies already in range and sight,
 * PartyBotAI::SelectResurrectionTarget / SelectReleasedCorpse).
 *
 * Update (bot update, out of combat, before the arrival walk, the teleport and the follow;
 * PartyBotAI checks the spell is ready and paid for):
 *   - nothing while a member fights (the healer is needed there);
 *   - the nearest dead member on this map within SEARCH_YD whose body (unreleased) or corpse
 *     (released) is out of range or sight of the resurrection, with a safe walk there
 *     (Perception/Spatial/SafeRoute: no idle pack on the way, at most MAX_PATH_DETOUR);
 *     no safe walk: that member is left for UNSAFE_RETRY_MS (the pack may wander off);
 *   - a navmesh walk to the body, made again every REPATH_MS;
 *   - ends once the body is in range and sight (the resurrection in UpdateAI takes it), when
 *     the member is alive or has a pending request, or when the bot got no PROGRESS_YD closer
 *     in STUCK_MS (that member is then left for GIVE_UP_MS).
 *
 * Map thread of the bot.
 */

#ifndef MANGOS_COMMANDER_RESURRECT_APPROACH_H
#define MANGOS_COMMANDER_RESURRECT_APPROACH_H

#include "Common.h"
#include "ObjectGuid.h"

class PartyBotAI;
class Player;
class SpellEntry;

namespace Commander
{
    // Per bot (PartyBotAI member). Plain data.
    struct ResurrectApproachState
    {
        ObjectGuid target;          // the dead member walked to
        uint32 nextMoveMs = 0;
        uint32 nextSearchMs = 0;    // nothing to walk to: when to look again
        float bestDist = 0.0f;      // the closest the bot got to the body
        uint32 bestMs = 0;          // when
        ObjectGuid left;            // a member not walked to until leftUntilMs (unsafe, stuck)
        uint32 leftUntilMs = 0;
    };

    namespace ResurrectApproach
    {
        constexpr float SEARCH_YD = 80.0f;
        constexpr float MAX_PATH_DETOUR = 3.0f;
        constexpr uint32 REPATH_MS = 2000;
        constexpr uint32 SEARCH_MS = 2000;
        constexpr float PROGRESS_YD = 3.0f;
        constexpr uint32 STUCK_MS = 15000;
        constexpr uint32 UNSAFE_RETRY_MS = 5000;
        constexpr uint32 GIVE_UP_MS = 60000;

        // Out of combat bot update, `resurrection` ready and affordable. True: the bot walks to
        // a body (PartyBotAI::UpdateAI does nothing else this tick: no arrival, teleport, follow).
        bool Update(PartyBotAI* bot, ResurrectApproachState& state, SpellEntry const* resurrection);

        // The walk ends (a resurrection is cast, the bot can no longer resurrect, combat).
        void Stop(Player* me, ResurrectApproachState& state);
    }
}

#endif
