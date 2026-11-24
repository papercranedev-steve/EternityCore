#include "Commander/Coordination/Situation/PushPlanner.h"
#include "Commander/Coordination/Situation/ClearPlanner.h"
#include "Commander/Coordination/Situation/Guide.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/BotDirectives.h"
#include "Commander/CommanderMgr.h"
#include "Commander/PartyCommander.h"
#include "Commander/Perception/Spatial/AdvanceRoute.h"
#include "Commander/Perception/Spatial/SpawnTargets.h"
#include "Commander/Perception/Tanks.h"
#include "TeamBotAI.h"
#include "Creature.h"
#include "Map.h"
#include "ObjectMgr.h"
#include "PathFinder.h"
#include "Player.h"
#include "Timer.h"
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <limits>

namespace Commander
{
namespace PushPlanner
{
    namespace
    {
        std::string Format(char const* fmt, ...)
        {
            char buf[256];
            va_list ap;
            va_start(ap, fmt);
            vsnprintf(buf, sizeof(buf), fmt, ap);
            va_end(ap);
            return buf;
        }

        Position3 PositionOf(WorldObject const* obj)
        {
            Position3 p;
            p.x = obj->GetPositionX();
            p.y = obj->GetPositionY();
            p.z = obj->GetPositionZ();
            return p;
        }

        float Dist3(Position3 const& a, Position3 const& b)
        {
            float const dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
            return std::sqrt(dx * dx + dy * dy + dz * dz);
        }

        bool Contains(std::vector<ObjectGuid> const& list, ObjectGuid guid)
        {
            return std::find(list.begin(), list.end(), guid) != list.end();
        }

        void Note(TeamPlan& plan, std::string text)
        {
            plan.notes.push_back(std::move(text));
        }

        std::string NameOf(Map* map, ObjectGuid guid)
        {
            if (Unit* u = map->GetUnit(guid))
                return u->GetName();
            if (CreatureInfo const* info = sObjectMgr.GetCreatureTemplate(guid.GetEntry()))
                return info->name;
            return "a creature";
        }

        // "Waiting for ..." once per reason, not every refresh.
        void Wait(TeamPlan& plan, std::string const& text)
        {
            if (plan.push.waitNoted == text)
                return;
            plan.push.waitNoted = text;
            Note(plan, text);
        }

        // Its own walk only (PushBehavior, owner "push"): a pull or a stance walk is not touched.
        void StopGuideWalk(Map* map, ObjectGuid guide)
        {
            Player* p = map ? map->GetPlayer(guide) : nullptr;
            TeamBotAI* ai = (p && p->AI()) ? dynamic_cast<TeamBotAI*>(p->AI()) : nullptr;
            if (!ai)
                return;
            char const* owner = ai->Directives().GetMovementOwner(WorldTimer::getMSTime());
            if (!owner || strcmp(owner, "push"))
                return;
            ai->Directives().ReleaseMovement();
            if (!p->IsStopped())
                p->StopMoving();
            p->GetMotionMaster()->Clear(false, true);
            p->GetMotionMaster()->MoveIdle();
        }

        void EndLeg(PushPlan& push, Map* map, ObjectGuid guide)
        {
            if (push.walking)
                StopGuideWalk(map, guide);
            push.walking = false;
        }

        void StartClear(TeamPlan& plan, PartyCommander& commander, Map* map, Player* guide)
        {
            PushPlan& push = plan.push;
            EndLeg(push, map, guide->GetObjectGuid());
            push.phase = PushPhase::Clear;
            push.waitNoted.clear();
            ClearPlanner::Start(guide, commander, push.radius, push.maxMobs, false);
        }

        void GiveUpGoal(TeamPlan& plan, Map* map, char const* why)
        {
            PushPlan& push = plan.push;
            if (why)
                Note(plan, "Going on without " + NameOf(map, push.goal) + ": " + why + ".");
            push.givenUp.push_back(push.goal);
            push.goal.Clear();
            push.goalFailures = 0;
        }

        // Everyone alive, near the tank and not resting. False: wait (or stopped).
        bool Gathered(TeamPlan& plan, PartyCommander& commander, WorldModel const& world, Map* map,
                      Position3 const& at)
        {
            for (PartyMemberFacts const& m : world.party)
            {
                if (m.isPet)
                    continue;
                if (!m.alive)
                {
                    Stop(commander, map, NameOf(map, m.guid) + " is dead.");
                    return false;
                }
                if (m.resting)
                {
                    Wait(plan, "Waiting for " + NameOf(map, m.guid) + " to finish eating and drinking.");
                    return false;
                }
                if (Dist3(m.pos, at) > GATHER_YD)
                {
                    Wait(plan, Format("Waiting for %s (more than %.0f yd behind).", NameOf(map, m.guid).c_str(), GATHER_YD));
                    return false;
                }
            }
            return true;
        }

        // The walk to `to` from the guide, -1 = no complete path.
        float WalkYd(Player* guide, Position3 const& to)
        {
            PathInfo path(guide);
            path.calculate(to.x, to.y, to.z);
            uint32 const type = path.getPathType();
            if ((type & (PATHFIND_NOPATH | PATHFIND_INCOMPLETE)) || path.getPath().empty())
                return -1.0f;
            if (type & PATHFIND_NOT_USING_PATH)
                return Dist3(PositionOf(guide), to);
            return path.Length();
        }

        // The next goal: among the nearest by straight line, the shortest walk. False: none left
        // (`why` says what is left).
        bool PickGoal(PushPlan& push, Map* map, Player* guide, std::string& why)
        {
            Position3 const from = PositionOf(guide);
            std::vector<SpawnTargets::Target> targets = SpawnTargets::Alive(map, guide);
            std::vector<std::string> bosses;
            targets.erase(std::remove_if(targets.begin(), targets.end(), [&](SpawnTargets::Target const& t)
            {
                if (t.boss)
                {
                    std::string const name = NameOf(map, t.guid);
                    if (std::find(bosses.begin(), bosses.end(), name) == bosses.end())
                        bosses.push_back(name);
                    return true;
                }
                return Contains(push.givenUp, t.guid);
            }), targets.end());
            std::sort(targets.begin(), targets.end(), [&from](SpawnTargets::Target const& a, SpawnTargets::Target const& b)
            {
                return Dist3(a.pos, from) < Dist3(b.pos, from);
            });

            uint32 paths = 0;
            for (size_t begin = 0; begin < targets.size() && paths < MAX_GOAL_PATHS; begin += GOAL_CANDIDATES)
            {
                SpawnTargets::Target const* best = nullptr;
                float bestYd = std::numeric_limits<float>::infinity();
                for (size_t i = begin; i < targets.size() && i < begin + GOAL_CANDIDATES && paths < MAX_GOAL_PATHS; ++i)
                {
                    ++paths;
                    float const yd = WalkYd(guide, targets[i].pos);
                    if (yd >= 0.0f && yd < bestYd)
                    {
                        bestYd = yd;
                        best = &targets[i];
                    }
                    else if (yd < 0.0f)
                        push.givenUp.push_back(targets[i].guid);    // no way from here: not again
                }
                if (best)
                {
                    push.goal = best->guid;
                    push.goalPos = best->pos;
                    push.goalFailures = 0;
                    return true;
                }
            }

            std::string bossText;
            for (std::string const& b : bosses)
                bossText += (bossText.empty() ? "" : ", ") + b;
            if (!targets.empty())
                why = "no way to the creatures left from here (a closed door?).";
            else
                why = "nothing left to clear.";
            if (!bossText.empty())
                why += " Bosses left: " + bossText + ". Pull them yourself (.partybot ai pull).";
            return false;
        }

        // The leg being walked: false while it goes on; true when it ended (arrived, stuck).
        bool LegOver(TeamPlan& plan, Map* map, Player* guide, uint32 nowMs)
        {
            PushPlan& push = plan.push;
            Position3 const here = PositionOf(guide);
            if (Dist3(here, push.legEnd) <= ARRIVED_YD)
            {
                push.walking = false;
                return true;
            }
            if (Dist3(here, push.lastPos) > 1.0f)
            {
                push.lastPos = here;
                push.lastMovedMs = nowMs;
            }
            bool const stuck = WorldTimer::getMSTimeDiff(push.lastMovedMs, nowMs) > STUCK_MS;
            if (!stuck && nowMs < push.legDeadlineMs)
                return false;
            EndLeg(push, map, guide->GetObjectGuid());
            if (++push.goalFailures >= MAX_GOAL_FAILURES)
                GiveUpGoal(plan, map, stuck ? "the tank gets stuck on the way" : "the walk takes too long");
            return true;
        }
    }

    bool Start(Player* leader, PartyCommander& commander, float radius, uint32 maxMobs, std::vector<std::string>& report)
    {
        Map* map = leader->GetMap();
        TeamPlan& plan = commander.GetPlanMutable();
        WorldModel const& world = commander.GetWorld();
        if (!map->IsDungeon())
        {
            report.push_back("Push works in dungeons only.");
            return false;
        }
        if (!sCommanderMgr.IsModuleEnabled("pull") || !sCommanderMgr.IsModuleEnabled("push"))
        {
            report.push_back("Modules 'pull' and 'push' must be on (.partybot ai module <id> on).");
            return false;
        }
        if (plan.retreat.active)
        {
            report.push_back("Retreating. '.partybot ai retreat stop' first.");
            return false;
        }
        PartyMemberFacts const* tankFacts = Tanks::Main(world, true);
        Player* tank = tankFacts ? map->GetPlayer(tankFacts->guid) : nullptr;
        if (!tank || !tank->IsAlive())
        {
            report.push_back("No living tank bot in your party here (.partybot setrole <name> tank).");
            return false;
        }

        ClearPlanner::Stop(commander, "the push starts.");
        ObjectGuid const before = plan.push.active ? plan.push.guideBefore : plan.guide;
        plan.push = PushPlan();
        PushPlan& push = plan.push;
        push.active = true;
        push.radius = radius;
        push.maxMobs = maxMobs;
        push.guideBefore = before;
        plan.guide = tank->GetObjectGuid();
        push.phase = PushPhase::Clear;
        ClearPlanner::Start(tank, commander, radius, maxMobs, false);

        std::string const limit = maxMobs ? Format("at most %u mobs per pull", maxMobs)
                                          : std::string("packs the fight estimate says are too hard skipped");
        report.push_back(Format("%s leads: it clears every pack within %.0f yd walk (%s), "
            "then walks on to the next living creature and clears again. The party follows %s.",
            tank->GetName(), radius, limit.c_str(), tank->GetName()));
        report.push_back(Format("Before each step everyone must be within %.0f yd of %s and done eating and drinking, "
            "you too. Bosses are yours. '.partybot ai push stop' to stop.", GATHER_YD, tank->GetName()));
        return true;
    }

    void Stop(PartyCommander& commander, Map* map, std::string const& why)
    {
        TeamPlan& plan = commander.GetPlanMutable();
        PushPlan& push = plan.push;
        if (!push.active)
            return;
        push.active = false;
        EndLeg(push, map, plan.guide);
        plan.guide = push.guideBefore;
        ClearPlanner::Stop(commander, "the push stopped.");
        Note(plan, Format("Push stopped after %u packs: %s The party follows you again.", push.cleared, why.c_str()));
    }

    void Step(TeamBotAI* bot, PartyCommander& commander, uint32 nowMs)
    {
        TeamPlan& plan = commander.GetPlanMutable();
        PushPlan& push = plan.push;
        if (!push.active)
            return;
        WorldModel const& world = commander.GetWorld();
        Map* map = bot->me->GetMap();

        Player* leader = bot->GetPartyLeader();
        if (!leader || !leader->IsInWorld() || leader->GetMap() != map)
        {
            Stop(commander, map, "you left the dungeon.");
            return;
        }
        if (plan.retreat.active)
        {
            Stop(commander, map, "retreating.");
            return;
        }
        if (plan.guide.IsEmpty())
        {
            Stop(commander, map, "the guide was changed (.partybot ai guide).");
            return;
        }
        if (!sCommanderMgr.IsModuleEnabled("push") || !sCommanderMgr.IsModuleEnabled("pull"))
        {
            Stop(commander, map, "module 'push' or 'pull' was turned off.");
            return;
        }

        if (push.phase == PushPhase::Clear)
        {
            if (plan.clear.active)
                return;
            if (plan.clear.lastEnd != ClearEnd::NothingLeft)
            {
                Stop(commander, map, "the clear stopped (see above).");
                return;
            }
            push.clearedLastRound = plan.clear.cleared;
            push.cleared += plan.clear.cleared;
            plan.clear.lastEnd = ClearEnd::None;
            push.phase = PushPhase::Advance;
            push.waitNoted.clear();
            return;
        }

        // Advance.
        Player* guide = Guide::Current(plan, leader);
        if (!guide)
        {
            Stop(commander, map, "the tank cannot lead (dead or gone).");
            return;
        }
        // A fight on the way (a patrol, the player pulled): the clear takes it.
        if (world.HasPartyEnemy() || !world.targeted.empty())
        {
            StartClear(plan, commander, map, guide);
            return;
        }
        if (push.walking && !LegOver(plan, map, guide, nowMs))
            return;

        Position3 const here = PositionOf(guide);
        if (!Gathered(plan, commander, world, map, here))
            return;

        // The goal died meanwhile (a clear killed it), or walked on (a patrol): follow it.
        if (!push.goal.IsEmpty())
        {
            if (Creature* c = map->GetCreature(push.goal))
            {
                if (!c->IsAlive())
                    push.goal.Clear();
                else
                    push.goalPos = PositionOf(c);
            }
        }
        if (push.goal.IsEmpty())
        {
            std::string why;
            if (!PickGoal(push, map, guide, why))
            {
                Stop(commander, map, why);
                return;
            }
        }

        AdvanceRoute::Leg const leg = AdvanceRoute::Toward(guide, push.goalPos, LEG_YD);
        if (!leg.path)
        {
            GiveUpGoal(plan, map, "no walkable way to it");
            return;
        }
        if (leg.yd < MIN_STEP_YD)
        {
            if (leg.blocker.IsEmpty())
            {
                // At the goal and nothing there to fight (it walked off, it is not spawned).
                GiveUpGoal(plan, map, nullptr);
                return;
            }
            // Something stands in the way: clear it. The same one again after a clear that
            // pulled nothing: the clear will not take it (a boss, too many mobs, too high).
            if (push.clearedLastRound == 0 && leg.blocker == push.lastBlocker)
            {
                Stop(commander, map, NameOf(map, leg.blocker) + " stands in the way and the clear does not pull it "
                    "(see above). Deal with it yourself, then '.partybot ai push' again.");
                return;
            }
            push.lastBlocker = leg.blocker;
            StartClear(plan, commander, map, guide);
            return;
        }

        push.waitNoted.clear();
        push.walking = true;
        push.legEnd = leg.end;
        push.lastPos = here;
        push.lastMovedMs = nowMs;
        float const speed = std::max(1.0f, guide->GetSpeed(MOVE_RUN));
        push.legDeadlineMs = nowMs + uint32(2.0f * leg.yd / speed * 1000.0f) + 4000;
    }
}
}
