/*
 * PartyBot Commander layer 3 (cerebellum) with its hands: a dead bot runs its corpse like a
 * player instead of rising where it fell (user, 2026-11-15).
 *
 * Body (not released):
 *   - a self resurrection (Soulstone, Reincarnation: PLAYER_SELF_RES_SPELL) is kept and used
 *     once it is safe (no member in combat, no enemy in combat within ENEMY_COMBAT_RADIUS, no
 *     idle enemy with the body in its aggro range + RISE_AGGRO_MARGIN: after a wipe they walk
 *     home, often to where the party fell), as the release dialog's button does
 *     (WorldSession::HandleSelfResOpcode). Idle enemies in the way for RISE_IDLE_WAIT_MS: the
 *     spirit is released after all;
 *   - else the spirit is released at once (WorldSession::HandleRepopRequestOpcode). A released
 *     corpse can still be resurrected: the bots cast at it (PartyBotAI::SelectReleasedCorpse),
 *     a player too; the ghost accepts (CombatBotBaseAI::OnPacketReceived) and the run ends.
 * Ghost (at the graveyard the engine chose): the run is simulated, the ghost stays there.
 *   - it takes TravelTime::Seconds from the graveyard to the corpse (to the entrance outside
 *     its dungeon) at the ghost's run speed, at least MIN_TRAVEL_SEC; unknown (another
 *     continent): UNKNOWN_TRAVEL_SEC;
 *   - meanwhile the party frames and the map show it on the straight line from the graveyard
 *     to there (Player::SetGroupShownPosition), moving with the time;
 *   - then: a corpse in a dungeon -> the ghost is put at the dungeon's entrance (the area
 *     trigger's destination) and the engine raises it there (Player::TeleportTo, as a player
 *     walking in); a corpse outside -> the ghost is put at its corpse and reclaims it once the
 *     reclaim delay is over (WorldSession::HandleReclaimCorpseOpcode); no corpse left -> it
 *     rises where it is;
 *   - back alive it walks to the leader (Behaviors/Movement/Arrival.h).
 *
 * Map thread of the bot.
 */

#ifndef MANGOS_COMMANDER_CORPSE_RUN_H
#define MANGOS_COMMANDER_CORPSE_RUN_H

#include "Common.h"
#include "SharedDefines.h"

class PartyBotAI;
class Player;

namespace Commander
{
    struct ArrivalState;

    // Per bot (PartyBotAI member). Plain data.
    struct CorpseRunState
    {
        enum class Phase : uint8
        {
            None,
            Released,   // repop done this tick; the run starts once the ghost is at the graveyard
            Running,    // simulated run under way
            AtCorpse,   // put at its corpse outside a dungeon, reclaiming
        };

        Phase phase = Phase::None;
        WorldLocation from;         // the graveyard
        WorldLocation to;           // the corpse, or the point outside its dungeon
        uint32 fromZone = 0;
        uint32 toZone = 0;
        uint32 startMs = 0;
        uint32 arriveMs = 0;
        // Body with a self resurrection: since when only idle enemies keep it from rising.
        uint32 idleSinceMs = 0;
    };

    namespace CorpseRun
    {
        constexpr float ENEMY_COMBAT_RADIUS = 40.0f;
        // Not rising within an idle enemy's aggro range + this (it would pull it at once), for
        // at most RISE_IDLE_WAIT_MS; then the spirit is released (the corpse run instead).
        constexpr float RISE_AGGRO_MARGIN = 5.0f;
        constexpr uint32 RISE_IDLE_WAIT_MS = 90 * 1000;
        constexpr uint32 MIN_TRAVEL_SEC = 10;
        constexpr uint32 UNKNOWN_TRAVEL_SEC = 120;

        // Dead bot update (PartyBotAI::UpdateAI). Starts `arrival` when the bot is alive again
        // away from its corpse.
        void Update(PartyBotAI* bot, CorpseRunState& state, ArrivalState& arrival);

        // Alive bot update: a run that ended another way (a resurrection) is cleared.
        void OnAlive(Player* me, CorpseRunState& state);
    }
}

#endif
