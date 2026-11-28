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
    namespace
    {
        void Face(Player* me, GestureState const& state, Player* p)
        {
            if (state.atSpot)
                me->SetFacingTo(me->GetAngle(state.spotX, state.spotY));
            else
                me->SetFacingToObject(p);
        }
    }

    void Want(GestureState& state, ObjectGuid to, uint32 textEmote, uint32 now)
    {
        if (!state.to.IsEmpty() && state.actAtMs)
            return;     // making one now
        state = GestureState();
        state.to = to;
        state.textEmote = textEmote;
        state.waitUntilMs = now + WAIT_MS;
    }

    void WantAt(GestureState& state, ObjectGuid to, uint32 textEmote, float x, float y, uint32 now)
    {
        if (!state.to.IsEmpty() && state.actAtMs)
            return;     // making one now
        state = GestureState();
        state.to = to;
        state.textEmote = textEmote;
        state.waitUntilMs = now + WAIT_MS;
        state.atSpot = true;
        state.spotX = x;
        state.spotY = y;
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
        bool const gone = state.atSpot ? me->GetDistance2d(state.spotX, state.spotY) > REACH_YD
                                       : !p || !p->IsInWorld() || !me->IsWithinDistInMap(p, REACH_YD);
        if (gone)
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
            if (!state.atSpot && !BotShared::Gaze::Sees(p, me))
                return false;
            // He looks (or it turns to his body): it stops and meets his eyes.
            Errands::StopWalk(me);
            Face(me, state, p);
            state.actAtMs = now + urand(EYE_CONTACT_MIN_MS, EYE_CONTACT_MAX_MS);
            return true;
        }

        if (!state.holdUntilMs)
        {
            if (now < state.actAtMs)
                return true;
            Face(me, state, p);
            // At his body only while it lies there (a ghost is elsewhere).
            Player* at = p && p->IsInWorld() && (!state.atSpot || p->GetDeathState() == CORPSE) ? p : nullptr;
            BotShared::BotChat::TextEmote(me, state.textEmote, at);
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
