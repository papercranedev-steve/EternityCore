#include "Life/Pastimes/Gesture.h"
#include "Life/Errands/ErrandCommon.h"
#include "Shared/BotChat.h"
#include "Shared/Gaze.h"
#include "Map.h"
#include "Player.h"
#include "Utilities/Random.h"

namespace Life
{
namespace Pastimes
{
namespace Gesture
{
    void Want(GestureState& state, ObjectGuid to, uint32 textEmote, uint32 now)
    {
        if (!state.to.IsEmpty() && state.actAtMs)
            return;     // making one now
        state = GestureState();
        state.to = to;
        state.textEmote = textEmote;
        state.waitUntilMs = now + WAIT_MS;
    }

    void Drop(GestureState& state)
    {
        state = GestureState();
    }

    bool Update(Player* me, GestureState& state, uint32 now)
    {
        if (state.to.IsEmpty())
            return false;
        Player* p = me->GetMap()->GetPlayer(state.to);
        if (!p || !p->IsInWorld() || !me->IsWithinDistInMap(p, REACH_YD))
        {
            Drop(state);
            return false;
        }

        if (!state.actAtMs)
        {
            if (now >= state.waitUntilMs)
            {
                Drop(state);
                return false;
            }
            if (!BotShared::Gaze::Sees(p, me))
                return false;
            // He looks: it stops and meets his eyes.
            Errands::StopWalk(me);
            me->SetFacingToObject(p);
            state.actAtMs = now + urand(EYE_CONTACT_MIN_MS, EYE_CONTACT_MAX_MS);
            return true;
        }

        if (!state.holdUntilMs)
        {
            if (now < state.actAtMs)
                return true;
            me->SetFacingToObject(p);
            BotShared::BotChat::TextEmote(me, state.textEmote, p);
            state.holdUntilMs = now + urand(HOLD_AFTER_MIN_MS, HOLD_AFTER_MAX_MS);
            return true;
        }

        if (now < state.holdUntilMs)
            return true;
        Drop(state);
        return false;
    }
}
}
}
