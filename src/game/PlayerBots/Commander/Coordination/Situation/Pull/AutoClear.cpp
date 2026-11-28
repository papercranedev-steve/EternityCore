#include "Commander/Coordination/Situation/Pull/AutoClear.h"
#include "Commander/Coordination/Situation/Pull/BossWaits.h"
#include "Commander/Coordination/Situation/Pull/Camps.h"
#include "Commander/Coordination/Situation/Pull/PackJudge.h"
#include "Commander/Coordination/Situation/Pull/PullQuestions.h"
#include "Commander/Coordination/Situation/Pull/PullStance.h"
#include "Commander/Coordination/Situation/ClearPlanner.h"
#include "Commander/CommanderMgr.h"
#include "Commander/PartyCommander.h"
#include "Commander/Perception/Spatial/PackResolver.h"
#include "Commander/Perception/Tanks.h"
#include "TeamBotAI.h"
#include "Formulas.h"
#include "Map.h"
#include "Player.h"
#include "Timer.h"
#include <algorithm>
#include <limits>

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

        float Distance(Position3 const& a, Position3 const& b)
        {
            return PackResolver::Distance(a, b);
        }

        void Note(TeamPlan& plan, std::string text)
        {
            plan.notes.push_back(std::move(text));
        }

        // He cannot be followed into a fight now.
        bool LeaderBusy(Player* leader)
        {
            return !leader->IsAlive() || leader->IsMounted() || leader->IsTaxiFlying() ||
                leader->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_RESTING) || leader->InBattleGround();
        }

        // The team does something else the player asked for. Members told to wait elsewhere
        // do not count, only the main tank who would pull.
        bool TeamBusy(TeamPlan const& plan, WorldModel const& world)
        {
            if (plan.push.active || plan.retreat.active || !plan.guide.IsEmpty())
                return true;
            PartyMemberFacts const* tank = Tanks::Main(world, true);
            return tank && plan.stay.Find(tank->guid);
        }

        // After the last clear ended (header: nothing left / any other end).
        bool MayStartAgain(AutoClearPlan const& autoClear, Player* leader, uint32 nowMs)
        {
            if (!autoClear.endedAtMs)
                return true;
            if (Distance(PositionOf(leader), autoClear.endedAt) >= MOVED_YD)
                return true;
            return autoClear.lastEnd == ClearEnd::NothingLeft &&
                WorldTimer::getMSTimeDiff(autoClear.endedAtMs, nowMs) >= RETRY_MS;
        }

        void Started(TeamPlan& plan, char const* note)
        {
            plan.autoClear.started = true;
            plan.autoClear.wasActive = true;
            Note(plan, note);
        }

        // ---- dungeon ----

        bool LeaderSettled(AutoClearPlan& autoClear, Player* leader, uint32 nowMs)
        {
            if (LeaderBusy(leader) || leader->IsInCombat())
            {
                autoClear.leaderStillSinceMs = 0;
                return false;
            }
            Position3 const here = PositionOf(leader);
            if (!autoClear.leaderStillSinceMs || Distance(here, autoClear.leaderPos) >= LEADER_STILL_RADIUS)
            {
                autoClear.leaderPos = here;
                autoClear.leaderStillSinceMs = nowMs;
                return false;
            }
            return WorldTimer::getMSTimeDiff(autoClear.leaderStillSinceMs, nowMs) >= LEADER_STILL_MS;
        }

        template <class Pred>
        bool AnyMember(PackFacts const& pack, Pred pred)
        {
            return std::any_of(pack.members.begin(), pack.members.end(), pred);
        }

        // A boss in front of him the clear would ask about now (header: no "wait" of his holds).
        PackFacts const* BossToAsk(AutoClearPlan& autoClear, WorldModel const& world, Player* leader)
        {
            for (PackFacts const& pack : world.packs)
            {
                if (!AnyMember(pack, [](PackCreature const& c) { return c.boss; }) ||
                    !AnyMember(pack, [](PackCreature const& c) { return c.aggressive && !c.noAggro; }))
                    continue;
                if (pack.route.state == PackRouteState::NoWay || pack.ApproachYd() > FRONT_WALK_YD ||
                    !ClearPlanner::IsInFront(world, pack))
                    continue;
                if (BossWaits::Holds(autoClear, pack, leader->GetSelectionGuid()))
                    continue;
                return &pack;
            }
            return nullptr;
        }

        void StepDungeon(TeamPlan& plan, PartyCommander& commander, WorldModel const& world, Player* leader, uint32 nowMs)
        {
            AutoClearPlan& autoClear = plan.autoClear;
            bool const idle = !plan.clear.active && plan.pull.phase == PullPhase::None && !TeamBusy(plan, world) &&
                !world.HasPartyEnemy();
            if (!idle)
                return;
            bool const settled = LeaderSettled(autoClear, leader, nowMs);
            // A boss in front not asked about yet: no wait after the last clear; selecting it:
            // not even the settle (once per boss, again after a "wait").
            PackFacts const* boss = BossToAsk(autoClear, world, leader);
            ObjectGuid const bossKey = boss ? ClearPlanner::PackKey(*boss).front() : ObjectGuid();
            bool const fresh = boss && bossKey != autoClear.bossStartFor;
            ObjectGuid const selection = leader->GetSelectionGuid();
            bool const selected = boss && !LeaderBusy(leader) && !leader->IsInCombat() &&
                AnyMember(*boss, [selection](PackCreature const& c) { return c.guid == selection; });
            if (!(fresh && (settled || selected)) && !(settled && MayStartAgain(autoClear, leader, nowMs)))
                return;
            if (fresh)
                autoClear.bossStartFor = bossKey;
            ClearPlanner::Start(leader, commander, FRONT_WALK_YD, 0);
            plan.clear.frontOnly = true;
            plan.clear.asks = true;
            Started(plan, "Active pull stance: clearing what is in front of you.");
        }

        // ---- open world ----

        // Fights he started himself (the eyes' engaged) that our pull did not: camps he wants.
        void RememberHisFights(TeamPlan& plan, WorldModel const& world, uint32 nowMs)
        {
            PullPlan const& pull = plan.pull;
            for (LeaderEngage const& fight : world.leaderActivity.engaged)
            {
                bool const ours = pull.target == fight.creature ||
                    std::find(pull.expected.begin(), pull.expected.end(), fight.creature) != pull.expected.end();
                if (!ours)
                    Camps::RememberFight(plan.autoClear.camps, fight, nowMs);
            }
        }

        // Chooses the packs of the standing clear (starts it when there is one); returns the
        // nearest pack to ask about, if any.
        PackFacts const* StepWorld(TeamPlan& plan, PartyCommander& commander, WorldModel const& world, Map* map,
                                   Player* leader, uint32 nowMs)
        {
            AutoClearPlan& autoClear = plan.autoClear;
            ClearPlan& clear = plan.clear;
            Camps::Forget(autoClear.camps, PositionOf(leader), nowMs);
            RememberHisFights(plan, world, nowMs);

            std::vector<ObjectGuid> chosen;
            PackFacts const* askPack = nullptr;
            bool const blocked = LeaderBusy(leader) || TeamBusy(plan, world) || leader->GetMap() != map;
            if (!blocked)
            {
                PackJudge::Input const in{ autoClear.camps, leader->GetLevel(), MaNGOS::XP::GetGrayLevel(leader->GetLevel()),
                                           leader->GetSelectionGuid(), world.leaderActivity.wanders };
                float askYd = std::numeric_limits<float>::max();
                for (PackFacts const& pack : world.packs)
                {
                    if (pack.ApproachYd() > WORLD_RADIUS)
                        continue;
                    int32 camp = -1;
                    switch (PackJudge::Judge(pack, in, camp))
                    {
                        case PackJudge::Verdict::Pull:
                            for (PackCreature const& c : pack.members)
                                chosen.push_back(c.guid);
                            if (camp >= 0)
                                Camps::Grow(autoClear.camps[camp], pack);
                            break;
                        case PackJudge::Verdict::Ask:
                            if (pack.ApproachYd() < askYd)
                            {
                                askYd = pack.ApproachYd();
                                askPack = &pack;
                            }
                            break;
                        case PackJudge::Verdict::Exclude:
                            break;
                    }
                }
            }

            if (clear.active)
            {
                // (a `.partybot ai clear` of his own pulls everything: nothing to ask)
                if (!clear.standing)
                    return nullptr;
                clear.chosen = std::move(chosen);
                return askPack;
            }
            if (blocked || chosen.empty() || plan.pull.phase != PullPhase::None || world.HasPartyEnemy() ||
                !MayStartAgain(autoClear, leader, nowMs))
                return askPack;

            ClearPlanner::Start(leader, commander, WORLD_RADIUS, 0);
            clear.standing = true;
            clear.chosenOnly = true;
            clear.asks = true;
            clear.chosen = std::move(chosen);
            Started(plan, "Active pull stance: pulling what you need around you.");
            return askPack;
        }
    }

    void Step(TeamBotAI* bot, PartyCommander& commander, uint32 nowMs)
    {
        TeamPlan& plan = commander.GetPlanMutable();
        AutoClearPlan& autoClear = plan.autoClear;
        WorldModel const& world = commander.GetWorld();
        Map* map = bot->me->GetMap();
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

        if (!leader || !world.humanInTeam || !leader->IsInWorld() || leader->GetMap() != map)
            return;

        if (sCommanderMgr.GetPullStance(leader->GetObjectGuid()) != PullStance::Active)
        {
            PullQuestions::Drop(plan);
            if (autoClear.started && plan.clear.active)
            {
                // A pull waiting for everyone to be ready is called off; one under way is finished.
                if (plan.pull.phase == PullPhase::WaitReady)
                    plan.pull = PullPlan();
                ClearPlanner::Stop(commander, "you start the fights yourself now.");
                autoClear.started = false;
                autoClear.wasActive = false;
            }
            // His own pulls wait for the party too: who has nothing to drink is still asked.
            PullQuestions::Step(plan, world, map, nullptr, nowMs, false);
            return;
        }

        if (!sCommanderMgr.IsModuleEnabled("pull") || !Tanks::Main(world, true))
            return;

        PackFacts const* askPack = nullptr;
        if (world.mapKind == MapKind::World)
        {
            // (out of the dungeon: its "wait" answers are gone with it)
            autoClear.bossWaits.clear();
            autoClear.bossStartFor = ObjectGuid();
            askPack = StepWorld(plan, commander, world, map, leader, nowMs);
        }
        else
            StepDungeon(plan, commander, world, leader, nowMs);
        PullQuestions::Step(plan, world, map, askPack, nowMs);
    }
}
}
