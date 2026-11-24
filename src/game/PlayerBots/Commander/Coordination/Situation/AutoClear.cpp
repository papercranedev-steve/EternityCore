#include "Commander/Coordination/Situation/AutoClear.h"
#include "Commander/Coordination/Situation/ClearPlanner.h"
#include "Commander/Coordination/Situation/Pull/PullStance.h"
#include "Commander/CommanderMgr.h"
#include "Commander/PartyCommander.h"
#include "Commander/Perception/Spatial/PackResolver.h"
#include "Commander/Perception/Tanks.h"
#include "TeamBotAI.h"
#include "Creature.h"
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

        // ---- both ----

        // He cannot be followed into a fight now.
        bool LeaderBusy(Player* leader)
        {
            return !leader->IsAlive() || leader->IsMounted() || leader->IsTaxiFlying() ||
                leader->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_RESTING) || leader->InBattleGround();
        }

        // The team does something else the player asked for.
        bool TeamBusy(TeamPlan const& plan)
        {
            return plan.push.active || plan.retreat.active || !plan.guide.IsEmpty() || !plan.stay.members.empty();
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

        void StepDungeon(TeamPlan& plan, PartyCommander& commander, WorldModel const& world, Player* leader, uint32 nowMs)
        {
            AutoClearPlan& autoClear = plan.autoClear;
            bool const idle = !plan.clear.active && plan.pull.phase == PullPhase::None && !TeamBusy(plan) &&
                !world.HasPartyEnemy();
            if (!idle || !LeaderSettled(autoClear, leader, nowMs) || !MayStartAgain(autoClear, leader, nowMs))
                return;
            ClearPlanner::Start(leader, commander, ClearPlan().radius, ClearPlan().maxMobs);
            plan.clear.asks = true;
            autoClear.started = true;
            autoClear.wasActive = true;
            Note(plan, "Active pull stance: clearing around you.");
        }

        // ---- open world ----

        void SampleTrail(AutoClearPlan& autoClear, Player* leader, uint32 nowMs)
        {
            auto& trail = autoClear.leaderTrail;
            if (trail.empty() || WorldTimer::getMSTimeDiff(trail.back().atMs, nowMs) >= TRAIL_STEP_MS)
                trail.push_back({ nowMs, PositionOf(leader) });
            while (!trail.empty() && WorldTimer::getMSTimeDiff(trail.front().atMs, nowMs) > WANDER_MS + TRAIL_STEP_MS)
                trail.erase(trail.begin());
        }

        // He stays around here (also standing still), not on his way somewhere.
        bool Wanders(AutoClearPlan const& autoClear, Player* leader, uint32 nowMs)
        {
            auto const& trail = autoClear.leaderTrail;
            if (trail.empty() || WorldTimer::getMSTimeDiff(trail.front().atMs, nowMs) + TRAIL_STEP_MS < WANDER_MS)
                return false;
            Position3 const here = PositionOf(leader);
            return std::all_of(trail.begin(), trail.end(), [&here](AutoClearPlan::LeaderSample const& s)
            {
                return Distance(s.pos, here) <= WANDER_YD;
            });
        }

        void ForgetAreas(AutoClearPlan& autoClear, Position3 const& leaderPos, uint32 nowMs)
        {
            auto& areas = autoClear.areas;
            areas.erase(std::remove_if(areas.begin(), areas.end(), [&](AreaAnswer const& a)
            {
                return int32(a.untilMs - nowMs) <= 0 || Distance(a.center, leaderPos) > AREA_FORGET_YD;
            }), areas.end());
        }

        AreaAnswer const* AreaAt(AutoClearPlan const& autoClear, Position3 const& p)
        {
            for (AreaAnswer const& a : autoClear.areas)
                if (Distance(a.center, p) <= AREA_YD)
                    return &a;
            return nullptr;
        }

        enum class Verdict : uint8 { Pull, Ask, Exclude };

        struct Judging
        {
            WorldModel const& world;
            AutoClearPlan const& autoClear;
            uint32 leaderLevel;
            uint32 grayLevel;
            ObjectGuid selection;
            bool wanders;
        };

        // Header: Pull / Ask / Exclude.
        Verdict Judge(PackFacts const& pack, Judging const& j)
        {
            auto any = [&pack](auto pred) { return std::any_of(pack.members.begin(), pack.members.end(), pred); };

            if (any([](PackCreature const& c) { return c.boss || c.tappedByOther || !c.claimedBy.IsEmpty(); }))
                return Verdict::Exclude;
            if (pack.maxLevel > j.leaderLevel + MAX_LEVEL_ABOVE)
                return Verdict::Exclude;

            bool const selected = any([&j](PackCreature const& c) { return c.aggressive && c.guid == j.selection; });
            bool const comesAnyway = any([&j](PackCreature const& c)
            {
                if (!c.aggressive || c.noAggro || c.stealthed)
                    return false;
                for (PartyMemberFacts const& m : j.world.party)
                    if (m.alive && Distance(m.pos, c.pos) <= c.reach + FIGHT_MARGIN_YD)
                        return true;
                return false;
            });
            if (selected || comesAnyway)
                return Verdict::Pull;

            // Uncertain: what he answered for this area, else ask (`anywhere`: also while he travels).
            auto uncertain = [&](bool anywhere)
            {
                if (AreaAnswer const* area = AreaAt(j.autoClear, pack.center))
                    return area->clear ? Verdict::Pull : Verdict::Exclude;
                return anywhere || j.wanders ? Verdict::Ask : Verdict::Exclude;
            };

            bool const elite = pack.eliteCount > 0;
            if (any([](PackCreature const& c) { return c.questNeeded; }))
                return elite ? uncertain(true) : Verdict::Pull;

            bool const startsFights = any([](PackCreature const& c) { return c.aggressive && !c.noAggro; });
            bool const allGray = !any([&j](PackCreature const& c) { return c.level > j.grayLevel; });
            if (!startsFights || allGray)
                return Verdict::Exclude;
            return uncertain(false);
        }

        // Chooses the packs of the standing clear (starts it when there is one); returns the
        // nearest pack to ask about, if any.
        PackFacts const* StepWorld(TeamPlan& plan, PartyCommander& commander, WorldModel const& world, Map* map,
                                   Player* leader, uint32 nowMs)
        {
            AutoClearPlan& autoClear = plan.autoClear;
            ClearPlan& clear = plan.clear;
            SampleTrail(autoClear, leader, nowMs);
            ForgetAreas(autoClear, PositionOf(leader), nowMs);

            std::vector<ObjectGuid> chosen;
            PackFacts const* askPack = nullptr;
            bool const blocked = LeaderBusy(leader) || TeamBusy(plan) || leader->GetMap() != map;
            if (!blocked)
            {
                Judging const j{ world, autoClear, leader->GetLevel(), MaNGOS::XP::GetGrayLevel(leader->GetLevel()),
                                 leader->GetSelectionGuid(), Wanders(autoClear, leader, nowMs) };
                float askYd = std::numeric_limits<float>::max();
                for (PackFacts const& pack : world.packs)
                {
                    if (pack.ApproachYd() > WORLD_RADIUS)
                        continue;
                    switch (Judge(pack, j))
                    {
                        case Verdict::Pull:
                            for (PackCreature const& c : pack.members)
                                chosen.push_back(c.guid);
                            break;
                        case Verdict::Ask:
                            if (pack.ApproachYd() < askYd)
                            {
                                askYd = pack.ApproachYd();
                                askPack = &pack;
                            }
                            break;
                        case Verdict::Exclude:
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

            ClearPlanner::Start(leader, commander, WORLD_RADIUS, ClearPlan().maxMobs);
            clear.standing = true;
            clear.chosenOnly = true;
            clear.asks = true;
            clear.chosen = std::move(chosen);
            autoClear.started = true;
            autoClear.wasActive = true;
            Note(plan, "Active pull stance: pulling what you need around you.");
            return askPack;
        }

        // ---- questions ----

        void Apply(TeamPlan& plan, PullQuestion const& q, uint32 nowMs)
        {
            bool const yes = q.answer == PullQuestion::Answer::Yes;
            ClearPlan& clear = plan.clear;
            if (q.kind == PullAskKind::Area)
            {
                plan.autoClear.areas.push_back({ q.where, yes, nowMs + AREA_MEMORY_MS });
                Note(plan, yes ? "You want the creatures around here cleared." : "Leaving the creatures around here alone.");
                return;
            }
            if (clear.ask.first == q.first)
                clear.ask = ClearAsk();
            if (yes)
                clear.approved.insert(clear.approved.end(), q.pack.begin(), q.pack.end());
            else
            {
                clear.skipped.insert(clear.skipped.end(), q.pack.begin(), q.pack.end());
                ++clear.skippedPacks;
            }
            Note(plan, yes ? "You said yes: pulling it." : "You said no: leaving it.");
        }

        void Ask(TeamPlan& plan, ObjectGuid asker, PullAskKind kind, ObjectGuid first, uint32 count,
                 std::vector<ObjectGuid> pack, Position3 const& where, uint32 nowMs)
        {
            PullQuestion& q = plan.pullQuestion;
            q = PullQuestion();
            q.id = plan.autoClear.nextQuestionId++;
            q.kind = kind;
            q.asker = asker;
            q.first = first;
            q.count = count;
            q.pack = std::move(pack);
            q.where = where;
            q.sinceMs = nowMs;
        }

        void Questions(TeamPlan& plan, WorldModel const& world, Map* map, Player* leader, PackFacts const* areaPack,
                       uint32 nowMs)
        {
            PullQuestion& q = plan.pullQuestion;
            if (q.id)
            {
                if (q.answer == PullQuestion::Answer::Pending &&
                    WorldTimer::getMSTimeDiff(q.sinceMs, nowMs) < QUESTION_MAX_MS)
                    return;
                Apply(plan, q, nowMs);  // (no answer in time: Pending counts as no)
                q = PullQuestion();
                return;
            }
            if (world.HasPartyEnemy())
                return;
            PartyMemberFacts const* tank = Tanks::Main(world, true);
            if (!tank)
                return;

            ClearAsk& ask = plan.clear.ask;
            if (!ask.pack.empty())
            {
                Creature const* first = plan.clear.active ? map->GetCreature(ask.first) : nullptr;
                if (!first || !first->IsAlive() || first->IsInCombat())
                    ask = ClearAsk();
                else
                {
                    Ask(plan, tank->guid, ask.kind, ask.first, ask.count, ask.pack, PositionOf(leader), nowMs);
                    return;
                }
            }
            if (!areaPack)
                return;
            ObjectGuid first = areaPack->members.front().guid;
            for (PackCreature const& c : areaPack->members)
                if (c.aggressive && !c.noAggro)
                {
                    first = c.guid;
                    break;
                }
            Ask(plan, tank->guid, PullAskKind::Area, first, 0, {}, PositionOf(leader), nowMs);
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
            plan.pullQuestion = PullQuestion();
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

        PackFacts const* askPack = nullptr;
        if (world.mapKind == MapKind::World)
            askPack = StepWorld(plan, commander, world, map, leader, nowMs);
        else
            StepDungeon(plan, commander, world, leader, nowMs);
        Questions(plan, world, map, leader, askPack, nowMs);
    }

    bool OpenQuestion(TeamPlan const& plan, PullQuestion& out)
    {
        if (!plan.pullQuestion.id || plan.pullQuestion.answer != PullQuestion::Answer::Pending)
            return false;
        out = plan.pullQuestion;
        return true;
    }

    void Answer(TeamPlan& plan, uint32 id, bool yes)
    {
        PullQuestion& q = plan.pullQuestion;
        if (q.id == id && q.answer == PullQuestion::Answer::Pending)
            q.answer = yes ? PullQuestion::Answer::Yes : PullQuestion::Answer::No;
    }
}
}
