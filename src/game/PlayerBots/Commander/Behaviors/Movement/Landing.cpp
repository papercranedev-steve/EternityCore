#include "Commander/Behaviors/Movement/Landing.h"
#include "Commander/BotDirectives.h"
#include "PartyBotAI.h"
#include "Player.h"
#include "Map.h"
#include "GridMap.h"
#include "MotionMaster.h"
#include "MoveSpline.h"
#include "MoveSplineInit.h"
#include "Log.h"
#include "Timer.h"
#include <algorithm>

namespace Commander
{
namespace Landing
{
    namespace
    {
        // The height the bot comes down to: the ground, or the water surface above it.
        // INVALID_HEIGHT: no ground within MAX_DROP_YD.
        float LandingZ(Player const* me)
        {
            float const x = me->GetPositionX();
            float const y = me->GetPositionY();
            float const z = me->GetPositionZ();
            float const ground = me->GetMap()->GetHeight(x, y, z + SEARCH_UP_YD, true, MAX_DROP_YD);
            if (ground <= INVALID_HEIGHT)
                return INVALID_HEIGHT;
            float const water = me->GetTerrain()->GetWaterLevel(x, y, z);
            return water > INVALID_HEIGHT ? std::max(ground, water) : ground;
        }
    }

    void OnTeleport(LandingState& state)
    {
        state.pending = true;
    }

    void Update(PartyBotAI* bot, LandingState& state)
    {
        if (!state.pending)
            return;
        Player* me = bot->me;
        if (!me->IsInWorld() || me->IsBeingTeleported())
            return;
        state.pending = false;

        if (me->GetTransport() || me->IsTaxiFlying() || me->IsInWater() || !me->movespline->Finalized() ||
            me->HasUnitState(UNIT_STATE_ROOT))
            return;

        float const landZ = LandingZ(me);
        float const drop = me->GetPositionZ() - landZ;
        if (landZ <= INVALID_HEIGHT || drop < MIN_DROP_YD)
            return;

        me->GetMotionMaster()->Clear(false, true);
        me->GetMotionMaster()->MoveIdle();
        Movement::PointsArray path;
        path.push_back(Movement::Vector3(me->GetPositionX(), me->GetPositionY(), me->GetPositionZ()));
        path.push_back(Movement::Vector3(me->GetPositionX(), me->GetPositionY(), landZ));
        Movement::MoveSplineInit init(*me, SOURCE);
        init.MovebyPath(path);
        init.SetFall();
        int32 const ms = init.Launch();
        if (ms <= 0)
            return;
        state.splineId = me->movespline->GetId();
        // Nothing starts a walk in the air.
        bot->Directives().HoldMovement(WorldTimer::getMSTime(), uint32(ms) + 300, SOURCE);
        sLog.Out(LOG_BASIC, LOG_LVL_BASIC, "[PartyBot Commander] %s lands after a teleport: %.1f yd down",
                 me->GetName(), drop);
    }

    bool IsFalling(Player const* me, LandingState const& state)
    {
        return state.splineId && me->movespline->GetId() == state.splineId && !me->movespline->Finalized();
    }
}
}
