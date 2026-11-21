/*
 * Bot daily life (PlayerBots/Life), pastimes: passing the time while waiting at one spot
 * (told to wait there: Commander/Coordination/Situation/Stay.h). It sits on the ground for a
 * while, gets up, stands about with the small gestures of a spot among people (PlaceActs), and
 * again. Where the spot is and whether it waits is the caller's.
 *
 * Update only while the bot stands at the spot, out of combat and not eating or drinking; End
 * whenever that stops (it stands up).
 *
 * Map thread of the bot.
 */

#ifndef MANGOS_LIFE_WAIT_AROUND_H
#define MANGOS_LIFE_WAIT_AROUND_H

#include "Common.h"
#include "Life/Pastimes/PlaceActs.h"

class Player;

namespace Life
{
    namespace Pastimes
    {
        struct WaitAroundState
        {
            ActState act;
            uint32 nextActMs = 0;   // 0: not waiting yet
        };

        namespace WaitAround
        {
            constexpr uint32 FIRST_ACT_MS = 6 * IN_MILLISECONDS;
            constexpr uint32 PAUSE_MIN_MS = 3 * IN_MILLISECONDS;
            constexpr uint32 PAUSE_MAX_MS = 10 * IN_MILLISECONDS;
            constexpr uint32 SIT_PCT = 60;

            void Update(Player* me, WaitAroundState& state, uint32 now);
            void End(Player* me, WaitAroundState& state);
            // Sitting on the ground now (the caller keeps it from being stood up).
            bool Seated(WaitAroundState const& state);
        }
    }
}

#endif
