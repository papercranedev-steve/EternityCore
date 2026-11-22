/*
 * Shared bot facts (PlayerBots/Shared): does a real player look at a bot now (user 2026-11-22:
 * a gesture he does not see is wasted, so bots gesture only when he looks).
 *
 * The server knows where his character faces, not his camera: turning with the right mouse
 * button, the keys or running sends it, only dragging the camera with the left button does not.
 * So "looks at it" is: he has it targeted, or it stands within SEE_YD in front of him (within
 * SEE_ARC around his facing) in line of sight. Dragging the camera onto it is missed (the bot
 * then skips its gesture, it never gestures at someone looking away).
 *
 * Map thread of `target`.
 */

#ifndef MANGOS_BOTSHARED_GAZE_H
#define MANGOS_BOTSHARED_GAZE_H

#include "Common.h"

class Player;
class WorldObject;

namespace BotShared
{
    namespace Gaze
    {
        constexpr float SEE_YD = 40.0f;
        // The whole arc in front of him (about 50 degrees to either side).
        constexpr float SEE_ARC = float(M_PI) * 5.0f / 9.0f;

        bool Sees(Player const* viewer, WorldObject const* target);
    }
}

#endif
