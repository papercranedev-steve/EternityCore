#include "Commander/Coordination/Situation/AutoClear.h"
#include "Commander/Coordination/Situation/ClearPlanner.h"
#include "Commander/Coordination/Situation/Pull/PullStance.h"
#include "Commander/CommanderMgr.h"
#include "Commander/PartyCommander.h"
#include "Commander/Perception/Spatial/PackResolver.h"
#include "Commander/Perception/Tanks.h"
#include "TeamBotAI.h"
#include "Player.h"
#include "Timer.h"

namespace Commander
{
namespace AutoClear
{
    namespace
    {
        Position3 PositionOf(WorldObject const* obj)
        {
            Position3 p;
            p.x = obj->GetPositionX();
            p.y = obj->GetPositionY();
            p.z = obj->GetPositionZ();
            return p;
        }

        // Nothing else the party does right now (header: "nothing else runs").
        bool PartyIdle(TeamPlan const& plan, WorldModel const& world)
        {
            return !plan.clear.active && !plan.push.active && !plan.retreat.active &&
                plan.pull.phase == PullPhase::None && plan.guide.IsEmpty() && plan.stay.members.empty() &&
                !world.HasPartyEnemy();
        }

        // The player is where he means to be, not on his way somewhere.
        bool LeaderSettled(AutoClearPlan& autoClear, Player* leader, uint32 nowMs)
        {
            if (!leader->IsAlive() || leader->IsMounted() || leader->IsTaxiFlying() || leader->IsInCombat() ||
                leader->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_RESTING) || leader->InBattleGround())
            {
                autoClear.leaderStillSinceMs = 0;
                return false;
            }
            Position3 const here = PositionOf(leader);
            if (!autoClear.leaderStillSinceMs || PackResolver::Distance(here, autoClear.leaderPos) >= LEADER_STILL_RADIUS)
            {
                autoClear.leaderPos = here;
                autoClear.leaderStillSinceMs = nowMs;
                return false;
            }
            return WorldTimer::getMSTimeDiff(autoClear.leaderStillSinceMs, nowMs) >= LEADER_STILL_MS;
        }

        // After the last clear ended (header: nothing left / any other end).
        bool MayStartAgain(AutoClearPlan const& autoClear, Player* leader, uint32 nowMs)
        {
            if (!autoClear.endedAtMs)
                return true;
            if (PackResolver::Distance(PositionOf(leader), autoClear.endedAt) >= MOVED_YD)
                return true;
            return autoClear.lastEnd == ClearEnd::NothingLeft &&
                WorldTimer::getMSTimeDiff(autoClear.endedAtMs, nowMs) >= RETRY_MS;
        }
    }

    void Step(TeamBotAI* bot, PartyCommander& commander, uint32 nowMs)
    {
        TeamPlan& plan = commander.GetPlanMutable();
        AutoClearPlan& autoClear = plan.autoClear;
        WorldModel const& world = commander.GetWorld();
        Player* leader = bot->GetPartyLeader();

        // A clear (ours or `.partybot ai clear`) just ended: remember where and how.
        if (autoClear.wasActive && !plan.clear.active)
        {
            autoClear.started = false;
            autoClear.lastEnd = plan.clear.lastEnd;
            autoClear.endedAtMs = nowMs;
            if (leader)
                autoClear.endedAt = PositionOf(leader);
        }
        autoClear.wasActive = plan.clear.active;

        if (!leader || !world.humanInTeam)
            return;

        bool const active = sCommanderMgr.GetPullStance(leader->GetObjectGuid()) == PullStance::Active;
        if (!active)
        {
            if (autoClear.started && plan.clear.active)
            {
                // A pull waiting for everyone to be ready is called off; one under way is finished.
                if (plan.pull.phase == PullPhase::WaitReady)
                    plan.pull = PullPlan();
                ClearPlanner::Stop(commander, "you start the fights yourself now.");
                autoClear.started = false;
                autoClear.wasActive = false;
            }
            return;
        }

        if (!sCommanderMgr.IsModuleEnabled("pull") || !Tanks::Main(world, true))
            return;
        if (!PartyIdle(plan, world))
            return;
        if (leader->GetMap() != bot->me->GetMap() || !leader->IsInWorld())
            return;
        if (!LeaderSettled(autoClear, leader, nowMs) || !MayStartAgain(autoClear, leader, nowMs))
            return;

        ClearPlanner::Start(leader, commander, ClearPlan().radius, ClearPlan().maxMobs);
        autoClear.started = true;
        autoClear.wasActive = true;
        plan.notes.push_back("Active pull stance: clearing around you.");
    }
}
}
