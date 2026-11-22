/*
 * Shared bot facts (PlayerBots/Shared): does a real player look at a bot now (user 2026-11-22:
 * a gesture he does not see is wasted, so bots gesture only when he looks).
 *
 * The server knows where his character faces, not his camera: turning with the right mouse
 * button, the keys or running sends it, only dragging the camera with the left button does not.
 * The PartyBot addon (PartyBot/Gaze.lua) fills that gap: it reports the players on his screen
 * (their nameplates shown) and under his mouse, through Shared/AddonChannel (prefix PBSEE: when
 * the set changes, and every few seconds while it is not empty). A report older than
 * SCREEN_FRESH_MS counts for nothing (the addon gone, the client closed).
 *
 * So "looks at it", within SEE_YD: he has it targeted; or, in line of sight, his addon reports it
 * on his screen or it stands in front of his character (within SEE_ARC around his facing).
 * Without the addon, dragging the camera onto it is missed (the bot then skips its gesture, it
 * never gestures at someone looking away).
 *
 * Sees: map thread of `target`. Reports: world thread (guarded).
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
        constexpr char const* REPORT_PREFIX = "PBSEE";
        // The addon resends every 3 s while something is on screen.
        constexpr uint32 SCREEN_FRESH_MS = 5000;
        constexpr size_t MAX_REPORTED = 32;

        // At startup: takes the addon's reports (AddonChannel).
        void Load();

        bool Sees(Player const* viewer, WorldObject const* target);
    }
}

#endif
