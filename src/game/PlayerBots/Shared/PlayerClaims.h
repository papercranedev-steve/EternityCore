/*
 * Shared bot facts (PlayerBots/Shared): which creatures a real player has claimed. Loot and
 * experience go to whoever hits a creature first, so a bot that has not hit it yet leaves a
 * creature a player is about to fight to him (population interactions, user 2026-11-22). Once
 * the bot hit it first there is nothing to give up.
 *
 * A player's side: a human, or a bot in a group with a human (his companions and party bots).
 * Claimed: one of them has it targeted and stands within CLAIM_YD of it.
 *
 * Map thread of `me`.
 */

#ifndef MANGOS_BOTSHARED_PLAYER_CLAIMS_H
#define MANGOS_BOTSHARED_PLAYER_CLAIMS_H

#include "Common.h"
#include "ObjectGuid.h"
#include <vector>

class Player;
class Unit;
class WorldObject;

namespace BotShared
{
    namespace PlayerClaims
    {
        constexpr float CLAIM_YD = 50.0f;

        struct Claim
        {
            ObjectGuid creature;
            ObjectGuid player;
            float x = 0.0f, y = 0.0f, z = 0.0f;     // where the player stands
        };

        // A human, or a bot grouped with a human.
        bool OnPlayersSide(Player const* p);

        // The targets of players on a human's side within `radius` (+ CLAIM_YD) of `center`;
        // not `me` nor anyone of his group.
        void Collect(Player const* me, WorldObject const* center, float radius, std::vector<Claim>& out);

        // Who claims `creature` among `claims` (within CLAIM_YD of it), empty: nobody.
        ObjectGuid ClaimantOf(std::vector<Claim> const& claims, Unit const* creature);
    }
}

#endif
