/*
 * Bot daily life (PlayerBots/Life), pastimes: a few people standing together talking (ambient
 * flow doc .kiro/docs/2026-11-22-ambient-flow.md §5, P3). An F-formation: 2-4 people on a small
 * circle, all facing its middle (the space they share), by a fire, in front of the inn, at the
 * flight master. One of them talks at a time (the talk animation, a turn of a few seconds), the
 * others listen (a nod, a laugh, a question now and then) and look at him; then someone else
 * has the word. At untilMs the talk is over and each goes his way.
 *
 * Shared by its members (one shared_ptr each, set up by whoever places them: Population's
 * TownDirector). Members are bots of one map, all updated on its thread. No speech: animations
 * only (as PlaceActs).
 */

#ifndef MANGOS_LIFE_PASTIMES_CHAT_CIRCLE_H
#define MANGOS_LIFE_PASTIMES_CHAT_CIRCLE_H

#include "Common.h"
#include "ObjectGuid.h"
#include <vector>

class Player;

namespace Life
{
namespace Pastimes
{
    struct ChatCircle
    {
        float x = 0.0f, y = 0.0f, z = 0.0f;     // the middle they face
        float radiusYd = 1.5f;
        float startAngle = 0.0f;
        uint32 size = 0;                         // slots
        uint32 untilMs = 0;                      // WorldTimer ms the talk ends
        // Filled by the members.
        std::vector<ObjectGuid> present;         // per slot: who stands there now (empty: nobody yet / gone)
        int32 speaker = -1;                      // slot that has the word, -1: nobody yet
        uint32 turnEndMs = 0;
        // One of them admires a passing player (Admire.h): the others look at him too until
        // watchUntilMs, a gesture or two.
        ObjectGuid watched;
        int32 watcher = -1;
        uint32 watchUntilMs = 0;
    };

    struct ChatSeat
    {
        uint32 slot = 0;
        bool joined = false;
        uint32 nextBeatMs = 0;
        int32 facing = -2;          // slot it looks at (-1: the middle), -2: not set yet, -3: the watched player
        uint32 watchReactAtMs = 0;  // != 0: its gesture at the watched player then
    };

    namespace ChatCircles
    {
        // Radius of a circle of `size` people (a little wider with more of them).
        float RadiusFor(uint32 size);
        // Where slot `slot` stands (x, y; z: the circle's), facing the middle.
        void SlotPosition(ChatCircle const& circle, uint32 slot, float& x, float& y);

        // The member stands at its slot: it joins (faces the middle).
        void Join(Player* me, ChatCircle& circle, ChatSeat& seat, uint32 now);
        // False: the talk is over (Leave next).
        bool Update(Player* me, ChatCircle& circle, ChatSeat& seat, uint32 now);
        // Gone from the circle (the talk is over, it leaves the town, a review).
        void Leave(Player* me, ChatCircle& circle, ChatSeat& seat);
        // The member at `seat` admires `player` until `untilMs` (it does that itself): the
        // others turn to him too.
        void Watch(ChatCircle& circle, ChatSeat const& seat, ObjectGuid player, uint32 untilMs);
        // Back to the talk (the watcher's admiring is over, or it was stopped).
        void FaceTalkAgain(ChatSeat& seat);
    }
}
}

#endif
