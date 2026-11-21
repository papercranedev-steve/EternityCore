#include "Life/Errands/WalkAway.h"
#include "Life/Errands/Travel.h"
#include "Life/Requests/Phrases.h"
#include "Shared/BotChat.h"
#include "Player.h"
#include "MotionMaster.h"
#include "Timer.h"
#include <cmath>

namespace Life
{
namespace WalkAway
{
    namespace
    {
        // Out of his sight, the way from him through the bot first.
        bool StartWalk(Player* me, WalkAwayState& state, Player* watcher)
        {
            float const angle = std::atan2(me->GetPositionY() - watcher->GetPositionY(), me->GetPositionX() - watcher->GetPositionX());
            if (!Travel::SpotOutOfSight(me, watcher, angle, state.spot))
                return false;
            state.phase = WalkAwayState::Phase::Walking;
            state.nextMoveMs = 0;
            return true;
        }
    }

    bool Begin(Player* me, WalkAwayState& state, Player* watcher, std::vector<std::string> const& tags)
    {
        state = WalkAwayState();
        if (!me->IsAlive() || !Travel::InSight(me, watcher))
            return false;
        state.untilMs = WorldTimer::getMSTime() + MAX_MS;
        me->GetMotionMaster()->Clear(false, true);
        me->GetMotionMaster()->MoveIdle();
        if (me->GetStandState() != UNIT_STAND_STATE_STAND)
            me->SetStandState(UNIT_STAND_STATE_STAND);

        bool const hearth = Travel::HearthReady(me) && !Travel::NearHome(me) && Travel::UseHearth(me);
        if (hearth)
            state.phase = WalkAwayState::Phase::Hearthing;
        else if (!StartWalk(me, state, watcher))
            return false;

        Phrases::Vars vars;
        vars["leader"] = watcher->GetName();
        vars["home"] = Travel::AreaName(me->GetHomeBindAreaId(), watcher);
        std::string const text = Phrases::Say(hearth ? "leave.bye.hearth" : "leave.bye", tags, vars);
        if (!text.empty())
            BotShared::BotChat::WhisperTo(me, watcher, text);
        return true;
    }

    bool Update(Player* me, WalkAwayState& state, Player* watcher)
    {
        if (!IsActive(state))
            return false;
        uint32 const now = WorldTimer::getMSTime();
        bool const busy = me->IsNonMeleeSpellCasted(false) || me->IsBeingTeleported();
        if (now >= state.untilMs || (!busy && !Travel::InSight(me, watcher)))
        {
            state = WalkAwayState();
            return false;
        }

        if (state.phase == WalkAwayState::Phase::Hearthing)
        {
            if (busy)
                return true;
            // (the cast broke, he still sees it: it walks)
            if (!StartWalk(me, state, watcher))
            {
                state = WalkAwayState();
                return false;
            }
        }

        if (now >= state.nextMoveMs)
        {
            state.nextMoveMs = now + REPATH_MS;
            me->GetMotionMaster()->MovePoint(0, state.spot.x, state.spot.y, state.spot.z, MOVE_PATHFINDING | MOVE_WALK_MODE);
        }
        return true;
    }
}
}
