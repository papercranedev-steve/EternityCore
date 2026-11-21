#include "Life/Pastimes/WaitAround.h"
#include "Player.h"
#include "Utilities/Random.h"

namespace Life
{
namespace Pastimes
{
namespace WaitAround
{
    void Update(Player* me, WaitAroundState& state, uint32 now)
    {
        if (state.act.active)
        {
            if (PlaceActs::Update(me, state.act, now))
                return;
            PlaceActs::End(me, state.act);
            state.nextActMs = now + urand(PAUSE_MIN_MS, PAUSE_MAX_MS);
            return;
        }
        if (!state.nextActMs)
        {
            state.nextActMs = now + FIRST_ACT_MS;
            return;
        }
        if (now < state.nextActMs)
            return;
        // (Fire: the ground posture, sitting or kneeling; Spot: standing with small gestures)
        Place const place = urand(0, 99) < SIT_PCT ? Place::Fire : Place::Spot;
        PlaceActs::Begin(me, state.act, place, 0.0f, 0.0f, false, nullptr, now);
    }

    void End(Player* me, WaitAroundState& state)
    {
        if (state.act.active)
            PlaceActs::End(me, state.act);
        state = WaitAroundState();
    }

    bool Seated(WaitAroundState const& state)
    {
        return state.act.active && state.act.seated;
    }
}
}
}
