#include "Life/Pastimes/ChatCircle.h"
#include "Map.h"
#include "Player.h"
#include "SharedDefines.h"
#include "Utilities/Random.h"
#include <cmath>

namespace Life
{
namespace Pastimes
{
namespace ChatCircles
{
    namespace
    {
        // A turn with the word, and the talk animation again within it.
        uint32 const TURN_MIN_MS = 4000, TURN_MAX_MS = 9000;
        uint32 const TALK_AGAIN_MIN_MS = 2500, TALK_AGAIN_MAX_MS = 4000;
        // Listeners: a reaction now and then.
        uint32 const REACT_MIN_MS = 4000, REACT_MAX_MS = 9000;
        uint32 const REACT_PCT = 30;

        bool Present(ChatCircle const& c, int32 slot)
        {
            return slot >= 0 && size_t(slot) < c.present.size() && !c.present[slot].IsEmpty();
        }

        // Someone else of those present gets the word (`except`'s turn is over), -1: nobody.
        int32 NextSpeaker(ChatCircle const& c, int32 except)
        {
            std::vector<int32> others;
            for (int32 i = 0; i < int32(c.present.size()); ++i)
                if (Present(c, i) && i != except)
                    others.push_back(i);
            if (others.empty())
                return Present(c, except) ? except : -1;
            return others[urand(0, uint32(others.size()) - 1)];
        }

        uint32 Reaction()
        {
            uint32 const roll = urand(0, 99);
            return roll < 35 ? EMOTE_ONESHOT_YES : roll < 70 ? EMOTE_ONESHOT_LAUGH : roll < 90 ? EMOTE_ONESHOT_QUESTION : EMOTE_ONESHOT_NO;
        }

        void Face(Player* me, ChatCircle const& c, ChatSeat& seat, int32 slot)
        {
            if (seat.facing == slot)
                return;
            seat.facing = slot;
            if (slot >= 0)
                if (Player* other = me->GetMap()->GetPlayer(c.present[slot]))
                {
                    me->SetFacingToObject(other);
                    return;
                }
            me->SetFacingTo(me->GetAngle(c.x, c.y));
        }
    }

    float RadiusFor(uint32 size)
    {
        return size <= 2 ? 1.1f : size == 3 ? 1.4f : 1.7f;
    }

    void SlotPosition(ChatCircle const& circle, uint32 slot, float& x, float& y)
    {
        float const angle = circle.startAngle + float(slot) * 2.0f * float(M_PI) / float(std::max<uint32>(circle.size, 1));
        x = circle.x + std::cos(angle) * circle.radiusYd;
        y = circle.y + std::sin(angle) * circle.radiusYd;
    }

    void Join(Player* me, ChatCircle& circle, ChatSeat& seat, uint32 now)
    {
        if (circle.present.size() < circle.size)
            circle.present.resize(circle.size);
        if (seat.slot >= circle.present.size())
            return;
        circle.present[seat.slot] = me->GetObjectGuid();
        seat.joined = true;
        seat.facing = -2;
        seat.nextBeatMs = now + urand(REACT_MIN_MS, REACT_MAX_MS);
        if (me->IsSittingDown())
            me->SetStandState(UNIT_STAND_STATE_STAND);
        Face(me, circle, seat, -1);
        // Whoever comes first opens.
        if (!Present(circle, circle.speaker))
        {
            circle.speaker = int32(seat.slot);
            circle.turnEndMs = now + urand(TURN_MIN_MS, TURN_MAX_MS);
        }
    }

    bool Update(Player* me, ChatCircle& circle, ChatSeat& seat, uint32 now)
    {
        if (!seat.joined)
            return false;
        if (now >= circle.untilMs)
            return false;

        // The word goes round (whoever notices first: they all run on one thread).
        if (!Present(circle, circle.speaker) || now >= circle.turnEndMs)
        {
            circle.speaker = NextSpeaker(circle, circle.speaker);
            circle.turnEndMs = now + urand(TURN_MIN_MS, TURN_MAX_MS);
            if (circle.speaker == int32(seat.slot))
                seat.nextBeatMs = now;
        }

        if (circle.speaker == int32(seat.slot))
        {
            // Talking to all of them.
            Face(me, circle, seat, -1);
            if (now >= seat.nextBeatMs)
            {
                me->HandleEmoteCommand(urand(0, 99) < 85 ? EMOTE_ONESHOT_TALK : (urand(0, 1) ? EMOTE_ONESHOT_POINT : EMOTE_ONESHOT_EXCLAMATION));
                seat.nextBeatMs = now + urand(TALK_AGAIN_MIN_MS, TALK_AGAIN_MAX_MS);
            }
            return true;
        }

        // Listening to him.
        Face(me, circle, seat, circle.speaker);
        if (now >= seat.nextBeatMs)
        {
            if (urand(0, 99) < REACT_PCT)
                me->HandleEmoteCommand(Reaction());
            seat.nextBeatMs = now + urand(REACT_MIN_MS, REACT_MAX_MS);
        }
        return true;
    }

    void Leave(Player* /*me*/, ChatCircle& circle, ChatSeat& seat)
    {
        if (!seat.joined)
            return;
        seat.joined = false;
        if (seat.slot < circle.present.size())
            circle.present[seat.slot].Clear();
        if (circle.speaker == int32(seat.slot))
            circle.speaker = -1;
    }
}
}
}
