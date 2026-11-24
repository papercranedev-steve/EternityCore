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

        void ForgetCamps(AutoClearPlan& autoClear, Position3 const& leaderPos, uint32 nowMs)
        {
            auto& camps = autoClear.camps;
            camps.erase(std::remove_if(camps.begin(), camps.end(), [&](CampMemory const& c)
            {
                return int32(c.untilMs - nowMs) <= 0 ||
                    std::none_of(c.spots.begin(), c.spots.end(), [&](Position3 const& s) { return Distance(s, leaderPos) <= CAMP_FORGET_YD; });
            }), camps.end());
        }

        // The camp `pack` belongs to: a kind of it, near one of the camp's spots (newest first).
        CampMemory const* CampOf(AutoClearPlan const& autoClear, PackFacts const& pack)
        {
            for (CampMemory const& camp : autoClear.camps)
            {
                bool const kind = std::any_of(pack.members.begin(), pack.members.end(), [&camp](PackCreature const& c)
                {
                    return std::find(camp.entries.begin(), camp.entries.end(), c.entry) != camp.entries.end();
                });
                if (kind && std::any_of(camp.spots.begin(), camp.spots.end(), [&pack](Position3 const& s)
                    {
                        return Distance(s, pack.center) <= CAMP_YD;
                    }))
                    return &camp;
            }
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

        bool ComesAnyway(PackCreature const& c, WorldModel const& world);

        // Header: Pull / Ask / Exclude. `campOut`: the camp memory that decided, if one did.
        Verdict Judge(PackFacts const& pack, Judging const& j, CampMemory const*& campOut)
        {
            campOut = nullptr;
            auto any = [&pack](auto pred) { return std::any_of(pack.members.begin(), pack.members.end(), pred); };

            if (any([](PackCreature const& c) { return c.boss || c.tappedByOther || !c.claimedBy.IsEmpty(); }))
                return Verdict::Exclude;
            if (pack.maxLevel > j.leaderLevel + MAX_LEVEL_ABOVE)
                return Verdict::Exclude;

            bool const selected = any([&j](PackCreature const& c) { return c.aggressive && c.guid == j.selection; });
            bool const comesAnyway = any([&j](PackCreature const& c) { return ComesAnyway(c, j.world); });
            if (selected || comesAnyway)
                return Verdict::Pull;

            bool const quest = any([](PackCreature const& c) { return c.questNeeded; });
            bool const elite = pack.eliteCount > 0;
            if (quest && !elite)
                return Verdict::Pull;

            // A camp he fought himself or answered about (also "别管" for a quest's elites).
            if (CampMemory const* camp = CampOf(j.autoClear, pack))
            {
                campOut = camp;
                return camp->pull ? Verdict::Pull : Verdict::Exclude;
            }
            if (quest)
                return Verdict::Ask;    // (an elite a quest needs: also while he travels)

            bool const startsFights = any([](PackCreature const& c) { return c.aggressive && !c.noAggro; });
            bool const allGray = !any([&j](PackCreature const& c) { return c.level > j.grayLevel; });
            if (!startsFights || allGray)
                return Verdict::Exclude;
            return j.wanders ? Verdict::Ask : Verdict::Exclude;
        }

        // A creature of `pack` comes to the party on its own (FIGHT_MARGIN_YD).
        bool ComesAnyway(PackCreature const& c, WorldModel const& world)
        {
            if (!c.aggressive || c.noAggro || c.stealthed)
                return false;
            for (PartyMemberFacts const& m : world.party)
                if (m.alive && Distance(m.pos, c.pos) <= c.reach + FIGHT_MARGIN_YD)
                    return true;
            return false;
        }

        // The camp `pack` was matched to grows by its spot (the camp is found as it is walked).
        void Grow(CampMemory& camp, PackFacts const& pack)
        {
            if (camp.spots.size() >= MAX_CAMP_SPOTS)
                return;
            for (Position3 const& s : camp.spots)
                if (Distance(s, pack.center) < CAMP_SPOT_STEP_YD)
                    return;
            camp.spots.push_back(pack.center);
        }

        std::vector<uint32> EntriesOf(PackFacts const& pack)
        {
            std::vector<uint32> entries;
            for (PackCreature const& c : pack.members)
                if (std::find(entries.begin(), entries.end(), c.entry) == entries.end())
                    entries.push_back(c.entry);
            return entries;
        }

        // He selected an idle creature that would not come on its own, and then a fight with it
        // started: he started it, its camp is one he wants (CampMemory::byPlayer).
        void WatchSelection(AutoClearPlan& autoClear, WorldModel const& world, Player* leader, uint32 nowMs)
        {
            if (!autoClear.watched.IsEmpty() && world.FindEnemy(autoClear.watched))
            {
                CampMemory* camp = nullptr;
                for (CampMemory& c : autoClear.camps)
                    if (c.byPlayer && std::find(c.entries.begin(), c.entries.end(), autoClear.watchedEntries.front()) != c.entries.end() &&
                        std::any_of(c.spots.begin(), c.spots.end(), [&](Position3 const& s) { return Distance(s, autoClear.watchedPos) <= CAMP_YD; }))
                    {
                        camp = &c;
                        break;
                    }
                if (camp)
                {
                    camp->untilMs = nowMs + STARTED_MEMORY_MS;
                    if (camp->spots.size() < MAX_CAMP_SPOTS)
                        camp->spots.push_back(autoClear.watchedPos);
                }
                else
                {
                    CampMemory fresh;
                    fresh.entries = autoClear.watchedEntries;
                    fresh.spots.push_back(autoClear.watchedPos);
                    fresh.pull = true;
                    fresh.byPlayer = true;
                    fresh.untilMs = nowMs + STARTED_MEMORY_MS;
                    autoClear.camps.insert(autoClear.camps.begin(), std::move(fresh));
                }
                autoClear.watched = ObjectGuid();
                autoClear.watchedEntries.clear();
            }

            ObjectGuid const selection = leader->GetSelectionGuid();
            for (PackFacts const& pack : world.packs)
                for (PackCreature const& c : pack.members)
                    if (c.guid == selection)
                    {
                        if (ComesAnyway(c, world) || c.tappedByOther)
                            return;
                        autoClear.watched = c.guid;
                        autoClear.watchedEntries = EntriesOf(pack);
                        autoClear.watchedPos = c.pos;
                        return;
                    }
            // (selected none idle: the old one stays watched while it is still about, in a fight just begun)
            if (!autoClear.watched.IsEmpty() && !world.FindEnemy(autoClear.watched) && selection != autoClear.watched)
            {
                autoClear.watched = ObjectGuid();
                autoClear.watchedEntries.clear();
            }
        }

        // Chooses the packs of the standing clear (starts it when there is one); returns the
        // nearest pack to ask about, if any.
        PackFacts const* StepWorld(TeamPlan& plan, PartyCommander& commander, WorldModel const& world, Map* map,
                                   Player* leader, uint32 nowMs)
        {
            AutoClearPlan& autoClear = plan.autoClear;
            ClearPlan& clear = plan.clear;
            SampleTrail(autoClear, leader, nowMs);
            ForgetCamps(autoClear, PositionOf(leader), nowMs);
            WatchSelection(autoClear, world, leader, nowMs);

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
                    CampMemory const* camp = nullptr;
                    switch (Judge(pack, j, camp))
                    {
                        case Verdict::Pull:
                            for (PackCreature const& c : pack.members)
                                chosen.push_back(c.guid);
                            if (camp)
                                Grow(const_cast<CampMemory&>(*camp), pack);     // (one of autoClear.camps)
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
                CampMemory camp;
                camp.entries = q.entries;
                camp.spots.push_back(q.where);
                camp.pull = yes;
                camp.untilMs = nowMs + ANSWER_MEMORY_MS;
                plan.autoClear.camps.insert(plan.autoClear.camps.begin(), std::move(camp));
                Note(plan, yes ? "You want that camp cleared." : "Leaving that camp alone.");
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

        PullQuestion& Ask(TeamPlan& plan, ObjectGuid asker, PullAskKind kind, ObjectGuid first, uint32 nowMs)
        {
            PullQuestion& q = plan.pullQuestion;
            q = PullQuestion();
            q.id = plan.autoClear.nextQuestionId++;
            q.kind = kind;
            q.asker = asker;
            q.first = first;
            q.sinceMs = nowMs;
            return q;
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
                // (shown and no answer in time: no; never shown: dropped, asked again later)
                if (q.answer != PullQuestion::Answer::Pending || q.shown)
                    Apply(plan, q, nowMs);
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
                    PullQuestion& asked = Ask(plan, tank->guid, ask.kind, ask.first, nowMs);
                    asked.count = ask.count;
                    asked.pack = ask.pack;
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
            PullQuestion& asked = Ask(plan, tank->guid, PullAskKind::Area, first, nowMs);
            for (PackCreature const& c : areaPack->members)
                asked.pack.push_back(c.guid);
            asked.entries = EntriesOf(*areaPack);
            asked.where = areaPack->center;
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

    void MarkShown(TeamPlan& plan, uint32 id)
    {
        if (plan.pullQuestion.id == id)
            plan.pullQuestion.shown = true;
    }

    void Answer(TeamPlan& plan, uint32 id, bool yes)
    {
        PullQuestion& q = plan.pullQuestion;
        if (q.id == id && q.answer == PullQuestion::Answer::Pending)
            q.answer = yes ? PullQuestion::Answer::Yes : PullQuestion::Answer::No;
    }
}
}
