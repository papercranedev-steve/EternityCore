// Patrols around a pull (PullPatrolCheck.h).
#include "Commander/Coordination/Situation/Pull/PullPlanner.h"
#include "Commander/Coordination/Situation/Pull/PullCommon.h"
#include "Commander/Coordination/Situation/Pull/PullChoice.h"
#include "Commander/Coordination/Situation/Pull/PullPatrolCheck.h"
#include "Commander/Coordination/Situation/Pull/PullLosSearch.h"
#include "Commander/Coordination/Situation/ClearPlanner.h"
#include "Commander/Coordination/Situation/Los/LosBreak.h"
#include "Commander/Coordination/Situation/Los/LosHealerSpot.h"
#include "Commander/Coordination/Situation/Patrol/PatrolAssigner.h"
#include "Commander/Coordination/Situation/Pull/PullMethod.h"
#include "Commander/Coordination/Situation/Pull/PullOpening.h"
#include "Commander/Coordination/Situation/Pull/PullControl.h"
#include "Commander/Coordination/Situation/Pull/PullPlace.h"
#include "Commander/CommanderMgr.h"
#include "Commander/PartyCommander.h"
#include "Commander/Perception/Forecast/AssistPredictor.h"
#include "Commander/Perception/Forecast/FightLoad.h"
#include "Commander/Perception/Spatial/PackResolver.h"
#include "Commander/Perception/Spatial/PatrolTracker.h"
#include "Commander/Perception/Forecast/PullOdds.h"
#include "Commander/Perception/Spatial/SafeRoute.h"
#include "Commander/Perception/Spatial/ShotSpot.h"
#include "Commander/Perception/Tanks.h"
#include "PathFinder.h"
#include "Commander/Perception/Profiles/ProfileStore.h"
#include "Commander/Perception/Values/ValueModel.h"
#include "Commander/Perception/Values/ControlValue.h"
#include "TeamBotAI.h"
#include "Creature.h"
#include "CreatureGroups.h"
#include "DBCStores.h"
#include "Group.h"
#include "Item.h"
#include "ItemPrototype.h"
#include "Map.h"
#include "Player.h"
#include "SpellMgr.h"
#include "Timer.h"
#include "World.h"
#include <algorithm>
#include <map>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace Commander
{
    using namespace PullDetail;

    namespace
    {
        // Who a patrol calls for help where its walk brings it next to the pull: it starts
        // attacking the tank there (AssistPredictor::ExpandAt). Per track sample, computed the
        // first time a start needs it.
        struct CallSource
        {
            Unit* tank = nullptr;
            PartyMemberFacts const* tankFacts = nullptr;
            std::vector<ObjectGuid> exclude;    // the pull and every patrol: counted on their own
            float delaySec = 0.0f;              // CreatureFamilyAssistanceDelay

            std::vector<PatrolCall> const& At(PatrolArrival& a, int index) const
            {
                auto itr = a.calls.find(index);
                if (itr != a.calls.end())
                    return itr->second;
                std::vector<PatrolCall>& calls = a.calls[index];
                Map* map = a.patrol->GetMap();
                std::vector<AssistPredictor::Joiner> const joiners =
                    AssistPredictor::ExpandAt(a.patrol, a.pos[index], tank, exclude);
                std::string names;
                for (AssistPredictor::Joiner const& j : joiners)
                {
                    Creature const* c = map->GetCreature(j.guid);
                    if (!c || !c->IsAlive())
                        continue;
                    PatrolCall call;
                    call.guid = j.guid;
                    call.foe = FightLoad::FoeOf(c, *tankFacts);
                    call.foe.arriveSec = delaySec;
                    calls.push_back(call);
                    if (Trace::On())
                        names += Trace::Format("%s\"%s\"", names.empty() ? "" : ",", c->GetName());
                }
                if (Trace::On())
                    Trace::Emit("patrolcall", a.patrol->GetName(), Trace::Format(
                        "{\"g\":\"%s\",\"atSec\":%.1f,\"x\":%.1f,\"y\":%.1f,\"delay\":%.1f,\"calls\":[%s]}",
                        Trace::Guid(a.patrol->GetObjectGuid()).c_str(), a.atSec[index], a.pos[index].x,
                        a.pos[index].y, delaySec, names.c_str()));
                return calls;
            }
        };

        // The tank's navmesh walk from where it stands to `to` (empty: no complete path; the
        // timeline then takes the straight line).
        std::vector<Position3> WalkOf(Unit* tank, Position3 const& to)
        {
            std::vector<Position3> out;
            PathInfo path(tank);
            path.calculate(to.x, to.y, to.z);
            if (path.getPathType() & (PATHFIND_NOT_USING_PATH | PATHFIND_NOPATH | PATHFIND_INCOMPLETE))
                return out;
            for (auto const& v : path.getPath())
            {
                Position3 p;
                p.x = v.x;
                p.y = v.y;
                p.z = v.z;
                out.push_back(p);
            }
            return out;
        }

        // `pull`'s places with their times from the tank's go (PullTimeline::Presence), the
        // tank where it stands now. `packSpots`: the pulled creatures (patrols left out).
        // `contactSec`: the timeline's contact (the fight starts), -1 without a timeline.
        std::shared_ptr<std::vector<PullTimeline::Zone> const> PullPlaces(WorldModel const& world, Unit* tank,
            PullPlan const& pull, std::vector<Position3> const& packSpots, float& contactSec)
        {
            contactSec = -1.0f;
            auto zones = std::make_shared<std::vector<PullTimeline::Zone>>();
            PullTimeline::Input in;
            if (!PullPlanner::TimelineInput(world, pull, in))
            {
                // (no timeline: the old places, the whole time)
                for (Position3 const& s : packSpots)
                    zones->push_back({ PullTimeline::ZoneKind::Place, s, 0.0f, PullTimeline::FOREVER, "pack" });
                zones->push_back({ PullTimeline::ZoneKind::Party, pull.spot, 0.0f, PullTimeline::FOREVER, "party" });
                return zones;
            }
            in.tankAt = PositionOf(tank);
            std::vector<Position3> const walk = WalkOf(tank, in.openFrom);
            if (walk.size() >= 2)
            {
                float yd = 0.0f;
                for (size_t i = 1; i < walk.size(); ++i)
                    yd += PackResolver::Distance(walk[i - 1], walk[i]);
                in.pathYd = yd;
            }
            PullTimeline::Result const r = PullTimeline::Of(in);
            contactSec = r.contactSec;
            *zones = PullTimeline::Presence(in, r, walk, packSpots, pull.spot);
            if (Trace::On())
                Trace::Emit("places", "pull's places", Trace::Format(
                    "{\"walkSec\":%.1f,\"openSec\":%.1f,\"contactSec\":%.1f,\"zones\":%u,\"walkPoints\":%u}",
                    r.walkSec, r.openSec, r.contactSec, uint32(zones->size()), uint32(std::count_if(zones->begin(),
                        zones->end(), [](PullTimeline::Zone const& z) { return z.kind == PullTimeline::ZoneKind::Walk; }))));
            return zones;
        }

        // Inputs of one start: the pull without the patrols plus every patrol that joins while
        // the fight lasts, from the time it arrives, and the creatures it calls for help there
        // (after the assistance delay; one called by two patrols counts once, the earliest).
        // Times: `fightSec` and the arrivals count from the tank's go, the fight (FightLoad's
        // 0 s) from `contactSec` (a patrol met on the walk is in the fight from its start).
        FightLoad::Result SimulateStart(FightLoad::Input base, std::vector<PatrolArrival>& patrols,
                                        CallSource const& source, float startSec, float fightSec, float contactSec)
        {
            std::string const why = Trace::On() ? Trace::Format("start +%.1f s, patrols joining", startSec) : std::string();
            std::map<ObjectGuid, size_t> called;    // guid -> index in base.foes
            for (PatrolArrival& p : patrols)
            {
                int const index = p.ArrivalIndex(startSec);
                if (index < 0)
                    continue;
                float const arrive = p.atSec[index] - startSec;
                if (arrive > fightSec)
                    continue;
                FightLoad::Foe f = p.foe;
                f.arriveSec = std::max(0.0f, arrive - contactSec);
                base.foes.push_back(f);
                for (PatrolCall const& c : source.At(p, index))
                {
                    float const at = f.arriveSec + c.foe.arriveSec;
                    auto itr = called.find(c.guid);
                    if (itr != called.end())
                    {
                        base.foes[itr->second].arriveSec = std::min(base.foes[itr->second].arriveSec, at);
                        continue;
                    }
                    FightLoad::Foe h = c.foe;
                    h.arriveSec = at;
                    called[c.guid] = base.foes.size();
                    base.foes.push_back(h);
                }
            }
            return FightLoad::Simulate(base, why.c_str());
        }
    }

    namespace PullDetail
    {
        // Patrols: WorldModel::patrols (out-of-combat path walkers within 100 yd of the leader),
        // including those the prediction already counts as joiners because they are walking
        // past the pack right now (strat-20261103-013312: Patchwork Horror was next to the
        // cadavers when the pull went, and joined 2 s after the shot). The target's own walking
        // group is not a patrol to it. A patrol next to any pulled creature is called in for
        // help, not only one next to the target (strat-20261102-235355).
        // The pull starts at the earliest time within `waitBudgetSec` the fight is bearable
        // (FightLoad), every patrol joining when its walk brings it next to the pull. None:
        // the start with the clearly best fight (WAIT_MIN_GAIN), else now (waiting does not
        // help).
        PatrolStart ChooseStart(WorldModel const& world, Map* map, Creature* target, Unit* tank, Unit* leader,
                                PullPlan const& pull, std::vector<ObjectGuid> const& expected, float waitBudgetSec)
        {
            PatrolStart out;
            Trace::Scope scope("patrolcheck");
            scope.Label("patrols around the pull of %s, wait budget %.0f s", target->GetName(), waitBudgetSec);
            PackCreature const* targetFacts = FindPackCreature(world, target->GetObjectGuid());
            bool const targetPatrols = targetFacts && targetFacts->patrol;
            CreatureGroup const* targetGroup = target->GetCreatureGroup();

            std::vector<Creature*> walkers;
            for (PackCreature const& c : world.patrols)
            {
                if (!c.aggressive || c.noAggro || c.tappedByOther || c.guid == target->GetObjectGuid())
                    continue;
                Creature* patrol = map->GetCreature(c.guid);
                if (!patrol || !patrol->IsAlive() || patrol->IsInCombat())
                    continue;
                if (targetGroup && patrol->GetCreatureGroup() == targetGroup)
                    continue;
                // A patrolling target walks with its neighbours (distance-linked, no group record).
                if (targetPatrols && patrol->GetDistance(target) <= PackResolver::LINK_DISTANCE)
                    continue;
                walkers.push_back(patrol);
            }

            // The pull without the walkers.
            std::vector<ObjectGuid> base;
            std::vector<Position3> pullAt;
            for (ObjectGuid g : expected)
            {
                if (std::any_of(walkers.begin(), walkers.end(), [g](Creature const* w) { return w->GetObjectGuid() == g; }))
                    continue;
                base.push_back(g);
                if (Creature const* m = map->GetCreature(g))
                    if (m->IsAlive())
                        pullAt.push_back(PositionOf(m));
            }
            if (!Contains(base, target->GetObjectGuid()))
            {
                base.push_back(target->GetObjectGuid());
                pullAt.push_back(PositionOf(target));
            }

            FightLoad::Input input;
            if (!FightLoad::Build(world, map, base, input))
            {
                out.hasTank = false;
                return out;
            }
            PartyMemberFacts const* tankFacts = Tanks::Main(world);
            FightLoad::Result const alone = FightLoad::Simulate(input, "without the patrols");

            // The pull's places and their times from the tank's go (header; PullTimeline::Presence).
            // The fight (FightLoad's 0 s) starts at contact; without a timeline as before: at
            // the go, PULL_LEAD_SEC added for the walk out and back.
            float contactSec = -1.0f;
            std::shared_ptr<std::vector<PullTimeline::Zone> const> const zones = PullPlaces(world, tank, pull, pullAt, contactSec);
            out.contactSec = std::max(0.0f, contactSec);
            out.fightSec = std::min(PullPlanner::PATROL_HORIZON_MAX_SEC, std::max(PullPlanner::PATROL_HORIZON_SEC,
                alone.fightSec + (contactSec >= 0.0f ? contactSec : PullPlanner::PULL_LEAD_SEC)));

            for (Creature* patrol : walkers)
            {
                PatrolArrival a;
                a.patrol = patrol;
                a.foe = FightLoad::FoeOf(patrol, *tankFacts);
                float const reachTank = patrol->GetAttackDistance(tank);
                a.clearance = std::max(PullPlanner::PATROL_MIN_CLEARANCE, reachTank);
                a.walkReach = reachTank + SafeRoute::TIGHT_MARGIN;
                a.partyReach = std::max(PullPlanner::PATROL_MIN_CLEARANCE, patrol->GetAttackDistance(leader ? leader : tank));
                a.zones = zones;
                PatrolTracker::Track const track = PatrolTracker::Predict(patrol, out.fightSec + waitBudgetSec);
                a.source = track.source;
                for (PatrolTracker::TrackPoint const& p : track.points)
                {
                    a.atSec.push_back(p.atSec);
                    a.pos.push_back(p.pos);
                }
                // Comes to the party for some start within the wait (a start every sample step).
                bool any = false;
                for (float s = 0.0f; !any && s <= waitBudgetSec + 0.001f; s += PatrolTracker::SAMPLE_STEP_SEC)
                    any = a.ArrivalIndex(s) >= 0;
                if (Trace::On())
                {
                    // When it comes to the party pulled now: windows of samples at one of the
                    // pull's places, and which place ("at", same order).
                    std::string windows, at;
                    for (size_t i = 0; i < a.atSec.size(); ++i)
                    {
                        PullTimeline::Zone const* z = a.ZoneAt(i, 0.0f);
                        if (!z || (i > 0 && a.ZoneAt(i - 1, 0.0f)))
                            continue;
                        size_t j = i;
                        while (j + 1 < a.atSec.size() && a.ZoneAt(j + 1, 0.0f))
                            ++j;
                        windows += Trace::Format("%s[%.1f,%.1f]", windows.empty() ? "" : ",", a.atSec[i], a.atSec[j]);
                        at += Trace::Format("%s\"%s\"", at.empty() ? "" : ",", z->what);
                    }
                    Trace::Emit("patrol", patrol->GetName(), Trace::Format(
                        "{\"g\":\"%s\",\"source\":\"%s\",\"clearance\":%.1f,\"walkReach\":%.1f,\"dps\":%.1f,\"hp\":%.0f,"
                        "\"horizon\":%.1f,\"inside\":[%s],\"at\":[%s]}",
                        Trace::Guid(patrol->GetObjectGuid()).c_str(), PatrolTracker::SourceName(a.source), a.clearance,
                        a.walkReach, a.foe.dps, a.foe.health, out.fightSec + waitBudgetSec, windows.c_str(), at.c_str()));
                }
                if (any)
                    out.patrols.push_back(std::move(a));
            }

            CallSource source;
            source.tank = tank;
            source.tankFacts = tankFacts;
            source.exclude = base;
            for (Creature const* w : walkers)
                source.exclude.push_back(w->GetObjectGuid());
            source.delaySec = sWorld.getConfig(CONFIG_UINT32_CREATURE_FAMILY_ASSISTANCE_DELAY) / 1000.0f;

            // Comes by only after the fight when pulled now and later: not part of this pull.
            out.now = SimulateStart(input, out.patrols, source, 0.0f, out.fightSec, out.contactSec);
            for (size_t i = 0; i < out.patrols.size(); ++i)
            {
                float const arrive = out.patrols[i].ArriveAfter(0.0f);
                if (arrive >= 0.0f && arrive <= out.fightSec && (out.mainIndex < 0 || arrive < out.mainArriveNow))
                {
                    out.mainIndex = int(i);
                    out.mainArriveNow = arrive;
                }
            }
            if (out.mainIndex < 0)
                return out;     // nobody joins a pull started now
            {
                PatrolArrival& main = out.patrols[out.mainIndex];
                out.mainCallsNow = uint32(source.At(main, main.ArrivalIndex(0.0f)).size());
            }
            out.considered = true;
            out.bearableNow = FightLoad::Bearable(out.now);
            out.atStart = out.now;
            out.bearableAtStart = out.bearableNow;
            if (out.bearableNow)
                return out;

            // Pull now, one patrol held when it comes (header).
            for (int pass = 0; pass < 2; ++pass)
                for (size_t i = 0; i < out.patrols.size(); ++i)
                {
                    float const arrive = out.patrols[i].ArriveAfter(0.0f);
                    if (arrive < 0.0f || arrive > out.fightSec)
                        continue;
                    std::vector<PatrolArrival> others;
                    for (size_t j = 0; j < out.patrols.size(); ++j)
                        if (j != i)
                            others.push_back(out.patrols[j]);
                    float const rest = SimulateStart(input, others, source, 0.0f, out.fightSec, out.contactSec).fightSec;
                    Creature const* patrol = out.patrols[i].patrol;
                    for (ControlValue::Option const& o : ControlValue::OptionsFor(world, patrol->GetEntry(), patrol->GetCreatureTypeMask()))
                    {
                        PartyMemberFacts const* b = world.FindMember(o.bot);
                        if (!o.ready || o.kind == ControlKind::Fear || o.areaDealer != (pass == 1) || !b || Tanks::IsTank(*b))
                            continue;
                        FightLoad::Input held = input;
                        FightLoad::Foe f = out.patrols[i].foe;
                        float const inFight = std::max(0.0f, arrive - out.contactSec);   // fight time
                        f.arriveSec = o.kind == ControlKind::Hold ? inFight + o.durationSec : std::max(inFight, rest);
                        f.sameFight = true;
                        held.foes.push_back(f);
                        FightLoad::Result const r = SimulateStart(held, others, source, 0.0f, out.fightSec, out.contactSec);
                        if (!FightLoad::Bearable(r))
                            continue;
                        out.holdIndex = int(i);
                        out.holdBot = o.bot;
                        out.holdSpellId = o.spellId;
                        out.atStart = r;
                        out.bearableAtStart = true;
                        return out;
                    }
                }

            // The earliest bearable start, else the best one.
            float bestSec = 0.0f;
            FightLoad::Result best = out.now;
            for (float s = PatrolTracker::SAMPLE_STEP_SEC; s <= waitBudgetSec + 0.001f; s += PatrolTracker::SAMPLE_STEP_SEC)
            {
                FightLoad::Result const r = SimulateStart(input, out.patrols, source, s, out.fightSec, out.contactSec);
                if (FightLoad::Bearable(r))
                {
                    out.startSec = s;
                    out.atStart = r;
                    out.bearableAtStart = true;
                    return out;
                }
                if (FightLoad::Score(r) > FightLoad::Score(best))
                {
                    best = r;
                    bestSec = s;
                }
            }
            if (bestSec > 0.0f && FightLoad::Score(best) >= FightLoad::Score(out.now) + PullPlanner::PATROL_WAIT_MIN_GAIN)
            {
                out.startSec = bestSec;
                out.atStart = best;
            }
            return out;
        }

        // "Patchwork Horror is next to the pull (path)" / "Patchwork Horror joins in about 12 s (path)"
        std::string DescribePatrol(PatrolStart const& s)
        {
            PatrolArrival const* main = s.Main();
            if (!main)
                return "";
            std::string const name = main->patrol->GetName();
            // Where it meets the party (the pull's place, PullTimeline::Presence).
            int const index = main->ArrivalIndex(0.0f);
            PullTimeline::Zone const* zone = index >= 0 ? main->ZoneAt(size_t(index), 0.0f) : nullptr;
            std::string const where = !zone ? std::string()
                : zone->kind == PullTimeline::ZoneKind::Walk ? std::string(" on the tank's walk")
                                                             : Format(" at the %s", zone->what);
            std::string const when = s.mainArriveNow <= 0.0f ? name + " is next to the pull" + where
                : Format("%s joins in about %.0f s%s", name.c_str(), s.mainArriveNow, where.c_str());
            float const yd = zone && zone->kind == PullTimeline::ZoneKind::Walk ? main->walkReach
                           : zone && zone->kind == PullTimeline::ZoneKind::Party ? main->partyReach : main->clearance;
            std::string const calls = s.mainCallsNow ? Format(", calling %u more for help there", s.mainCallsNow) : std::string();
            return when + Format(" (%s, within %.0f yd)", PatrolTracker::SourceName(main->source), yd) + calls;
        }

        // What the patrol check decided, for the report and the notes. `waiting`: "The pull
        // waits" (planned) or "Waiting" (held).
        std::string PatrolVerdict(PatrolStart const& s, char const* waiting)
        {
            std::string const head = "Patrol " + DescribePatrol(s) + ". ";
            if (s.bearableNow)
                return head + "The party can take it, pulling without waiting: " + FightLoad::Describe(s.now) + ".";
            if (PatrolArrival const* held = s.Held())
            {
                Player const* bot = held->patrol->GetMap()->GetPlayer(s.holdBot);
                SpellEntry const* spell = sSpellMgr.GetSpellEntry(s.holdSpellId);
                return head + "Pulled now: " + FightLoad::Describe(s.now) + ". Not waiting: " +
                    (bot ? bot->GetName() : "?") + " holds " + held->patrol->GetName() + " with " +
                    (spell ? spell->SpellName[0].c_str() : "?") + " when it comes: " + FightLoad::Describe(s.atStart) + ".";
            }
            if (s.startSec > 0.0f)
                return head + "Pulled now: " + FightLoad::Describe(s.now) + Format(". %s about %.0f s", waiting,
                    std::ceil(s.startSec)) + (s.bearableAtStart ? ": " : ", which makes it easier: ") +
                    FightLoad::Describe(s.atStart) + ".";
            return head + "Pulled now: " + FightLoad::Describe(s.now) +
                Format(". Waiting up to %u s does not make it easier.", PullPlanner::PATROL_WAIT_MAX_MS / 1000);
        }
    }
}
