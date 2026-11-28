#include "Life/Pastimes/CompanionBody.h"
#include "Player.h"
#include "SharedDefines.h"
#include "Utilities/Random.h"
#include <algorithm>

namespace Life
{
namespace Pastimes
{
namespace CompanionBody
{
    namespace
    {
        uint32 const g_congratulate[] = { TEXTEMOTE_CONGRATULATE, TEXTEMOTE_CHEER, TEXTEMOTE_APPLAUD, TEXTEMOTE_CLAP };
        uint32 const g_ownLevel[] = { TEXTEMOTE_CHEER, TEXTEMOTE_FLEX, TEXTEMOTE_HAPPY };
        uint32 const g_regard[] = { TEXTEMOTE_WAVE, TEXTEMOTE_NOD, TEXTEMOTE_SMILE, TEXTEMOTE_HELLO };

        template <size_t N>
        uint32 Pick(uint32 const (&list)[N])
        {
            return list[urand(0, uint32(N) - 1)];
        }

        bool Has(std::vector<std::string> const& tags, char const* tag)
        {
            return std::find(tags.begin(), tags.end(), tag) != tags.end();
        }

        uint32 CelebratePct(std::vector<std::string> const& tags)
        {
            return Has(tags, "chatty") ? CELEBRATE_CHATTY_PCT : Has(tags, "quiet") ? CELEBRATE_QUIET_PCT : CELEBRATE_PCT;
        }

        void Init(Player* me, CompanionBodyState& state)
        {
            if (state.init)
                return;
            EmoteReply::Init(state.reply, me->GetObjectGuid().GetRawValue());
            state.init = true;
        }

        // His level-up and its own: a gesture at him once he looks.
        void WatchLevels(Player* me, CompanionBodyState& state, Player* leader, std::vector<std::string> const& tags,
                         uint32 now)
        {
            if (state.leader != leader->GetObjectGuid())
            {
                state.leader = leader->GetObjectGuid();
                state.leaderLevel = 0;
            }
            uint32 const his = leader->GetLevel();
            uint32 const mine = me->GetLevel();
            if (state.leaderLevel && his > state.leaderLevel && urand(0, 99) < CelebratePct(tags))
                Gesture::Want(state.gesture, leader->GetObjectGuid(), Pick(g_congratulate), now);
            else if (state.myLevel && mine > state.myLevel && urand(0, 99) < CelebratePct(tags))
                Gesture::Want(state.gesture, leader->GetObjectGuid(), Pick(g_ownLevel), now);
            state.leaderLevel = his;
            state.myLevel = mine;
        }

        // He selects it near him: it turns to him (then now and then a wave or a nod).
        void Regard(Player* me, CompanionBodyState& state, Player* leader, bool canStand, uint32 now)
        {
            bool const selected = leader->GetSelectionGuid() == me->GetObjectGuid() && me->IsWithinDistInMap(leader, REGARD_YD);
            if (!selected)
            {
                state.regarded = false;
                state.regardAtMs = 0;
                return;
            }
            if (state.regarded || !canStand || me->IsMoving() || me->IsNonMeleeSpellCasted(false))
                return;
            if (!state.regardAtMs)
            {
                state.regardAtMs = std::max<uint32>(now + urand(REGARD_MIN_MS, REGARD_MAX_MS), 1);
                return;
            }
            if (now < state.regardAtMs)
                return;
            state.regarded = true;
            state.regardAtMs = 0;
            me->SetFacingToObject(leader);
            if (now >= state.nextRegardGestureMs && urand(0, 99) < REGARD_GESTURE_PCT)
            {
                state.nextRegardGestureMs = now + REGARD_GESTURE_GAP_MS;
                Gesture::Want(state.gesture, leader->GetObjectGuid(), Pick(g_regard), now);
            }
        }
    }

    void Heard(Player* me, CompanionBodyState& state, Player* from, uint32 textEmote, uint32 now)
    {
        Init(me, state);
        EmoteReply::Heard(me, state.reply, from, textEmote, now);
    }

    bool Update(Player* me, CompanionBodyState& state, Player* leader, bool free, bool canStand,
                std::vector<std::string> const& tags, uint32 now)
    {
        Init(me, state);
        if (!free || !leader)
        {
            Stop(me, state);
            return false;
        }
        WatchLevels(me, state, leader, tags, now);
        Regard(me, state, leader, canStand, now);

        bool const answering = EmoteReply::Update(me, state.reply, canStand, now);
        EmoteReply::TakeWalkOff(state.reply);     // (insulted: it stays with him all the same)
        if (answering)
            return true;
        // A gesture not begun waits while he walks on (it would stop and fall behind).
        if (!canStand && !state.gesture.actAtMs)
            return false;
        return Gesture::Update(me, state.gesture, now);
    }

    void Stop(Player* me, CompanionBodyState& state)
    {
        EmoteReply::Stop(me, state.reply);
        state.reply.from.Clear();
        Gesture::Drop(state.gesture);
        state.regarded = false;
        state.regardAtMs = 0;
    }
}
}
}
