/*
 * Bot daily life (PlayerBots/Life): a spot a bot can walk away from (user 2026-11-23: town bots
 * stood on a chair in Stormwind for minutes).
 *
 * A player bot's path ends in front of the first gameobject with collision (PathInfo::
 * CutPathWithDynamicLoS, for closed doors). A bot that got into an object's collision (walked up
 * to a chair's middle, or put onto its seat by GameObject::Use) hits the object at once in every
 * direction: each path is zero yards long and ends where it stands, so MovePoint goes nowhere
 * (Stormwind, 2026-11-23: three bots at a Wooden Chair for minutes). A client just walks out of
 * a model; a bot has to be put one step out.
 *
 * - Leavable: a point whose path back to a reference point (where the bot came from) gets there.
 * - Trapped: the bot's path to a destination a few yards away goes nowhere.
 * - FindExit: a spot close by (rings of points around the bot) whose path reaches the
 *   destination, for a near teleport (one step).
 *
 * Map thread of the bot.
 */

#ifndef MANGOS_LIFE_TRIP_STEP_OFF_H
#define MANGOS_LIFE_TRIP_STEP_OFF_H

#include "Common.h"

class Player;

namespace Life
{
namespace Trip
{
    namespace StepOff
    {
        struct Spot
        {
            float x = 0.0f, y = 0.0f, z = 0.0f;
        };

        // Exits are looked for this far from the bot at most.
        constexpr float EXIT_MAX_YD = 5.0f;

        // From `spot` the bot can walk to `back` (the full way, along the mesh) and from `back`
        // to `spot`.
        bool Leavable(Player* me, Spot const& spot, Spot const& back);
        // The bot is in a hole: `to` is farther than a step but the path there ends where it stands.
        bool Trapped(Player* me, Spot const& to);
        // A ground spot near the bot, in its line of sight, from which `to` can be walked to.
        bool FindExit(Player* me, Spot const& to, Spot& out);
        // Trapped on the way to `to`: one step onto FindExit's spot (a near teleport). False:
        // not trapped, or no exit.
        bool HopIfTrapped(Player* me, Spot const& to);
    }
}
}

#endif
