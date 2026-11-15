/*
 * PartyBot Commander layer 4 (hands): the drop to the ground after a teleport.
 *
 * A client falls by itself from where a teleport puts it; a bot has none, so it stayed at the
 * teleport point. Dungeon entrance points (areatrigger_teleport) and a leader's position in a
 * jump are in the air: the bot ghost hung there, rose there (the engine raises a ghost entering
 * its corpse's dungeon), and the clients around let it fall and put it back up at each of its
 * movement packets (a buff), until it sat down in the air to drink (user, 2026-11-15).
 *
 * OnTeleport: from PartyBotAI::OnPacketReceived (SMSG_NEW_WORLD, MSG_MOVE_TELEPORT_ACK).
 * Update, every frame: once the teleport is done and the bot in the world, ground (or water
 * surface) between MIN_DROP_YD and MAX_DROP_YD below it: one falling spline straight down, under
 * a movement hold (owner SOURCE) for its duration. While it falls the bot update returns at once
 * (IsFalling: a cast's StopMoving would leave it hanging again). Not on a transport, in water or
 * on a taxi.
 *
 * Map thread of the bot.
 */

#ifndef MANGOS_COMMANDER_LANDING_H
#define MANGOS_COMMANDER_LANDING_H

#include "Common.h"

class PartyBotAI;
class Player;

namespace Commander
{
    // Per bot (PartyBotAI member). Plain data.
    struct LandingState
    {
        bool pending = false;       // a teleport was sent: check the ground when it is done
        uint32 splineId = 0;        // the fall's spline (0 = none)
    };

    namespace Landing
    {
        constexpr char const* SOURCE = "Landing";
        constexpr float MIN_DROP_YD = 0.5f;
        constexpr float MAX_DROP_YD = 60.0f;
        constexpr float SEARCH_UP_YD = 0.5f;

        void OnTeleport(LandingState& state);

        // Every frame from PartyBotAI::UpdateAI.
        void Update(PartyBotAI* bot, LandingState& state);

        // In the fall's spline: the bot update does nothing.
        bool IsFalling(Player const* me, LandingState const& state);
    }
}

#endif
