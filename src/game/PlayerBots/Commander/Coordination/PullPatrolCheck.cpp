// Patrols around a pull (PullPatrolCheck.h).
#include "Commander/Coordination/PullPlanner.h"
#include "Commander/Coordination/PullCommon.h"
#include "Commander/Coordination/PullChoice.h"
#include "Commander/Coordination/PullPatrolCheck.h"
#include "Commander/Coordination/PullLosSearch.h"
#include "Commander/Coordination/ClearPlanner.h"
#include "Commander/Coordination/LosBreak.h"
#include "Commander/Coordination/LosHealerSpot.h"
#include "Commander/Coordination/PatrolAssigner.h"
#include "Commander/Coordination/PullMethod.h"
#include "Commander/Coordination/PullOpening.h"
#include "Commander/Coordination/PullControl.h"
#include "Commander/Coordination/PullPlace.h"
#include "Commander/CommanderMgr.h"
#include "Commander/PartyCommander.h"
#include "Commander/Perception/AssistPredictor.h"
#include "Commander/Perception/FightLoad.h"
#include "Commander/Perception/PackResolver.h"
#include "Commander/Perception/PatrolTracker.h"
#include "Commander/Perception/PullOdds.h"
#include "Commander/Perception/ShotSpot.h"
#include "Commander/Perception/Tanks.h"
#include "Commander/Perception/ProfileStore.h"
#include "Commander/Perception/ValueModel.h"
#include "PartyBotAI.h"
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
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace Commander
{
    using namespace PullDetail;

    namespace
    {
        // Inputs of one start: the pull without the patrols plus every patrol that joins while
        // the fight lasts, from the time it arrives.
        FightLoad::Result SimulateStart(FightLoad::Input base, std::vector<PatrolArrival> const& patrols,
                                        float startSec, float fightSec)
        {
            for (PatrolArrival const& p : patrols)
            {
                float const arrive = p.ArriveAfter(startSec);
                if (arrive < 0.0f || arrive > fightSec)
                    continue;
                FightLoad::Foe f = p.foe;
                f.arriveSec = arrive;
                base.foes.push_back(f);
            }
            return FightLoad::Simulate(base);
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
                                Position3 const& spot, std::vector<ObjectGuid> const& expected, float waitBudgetSec)
        {
            PatrolStart out;
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
            FightLoad::Result const alone = FightLoad::Simulate(input);
            out.fightSec = std::min(PullPlanner::PATROL_HORIZON_MAX_SEC,
                std::max(PullPlanner::PATROL_HORIZON_SEC, alone.fightSec + PullPlanner::PULL_LEAD_SEC));

            for (Creature* patrol : walkers)
            {
                PatrolArrival a;
                a.patrol = patrol;
                a.foe = FightLoad::FoeOf(patrol, *tankFacts);
                float const toTank = std::max(PullPlanner::PATROL_MIN_CLEARANCE, patrol->GetAttackDistance(tank));
                float const toParty = std::max(PullPlanner::PATROL_MIN_CLEARANCE, patrol->GetAttackDistance(leader ? leader : tank));
                a.clearance = toTank;
                PatrolTracker::Track const track = PatrolTracker::Predict(patrol, out.fightSec + waitBudgetSec);
                a.source = track.source;
                bool any = false;
                for (PatrolTracker::TrackPoint const& p : track.points)
                {
                    bool in = PackResolver::Distance(p.pos, spot) <= toParty;
                    for (size_t i = 0; !in && i < pullAt.size(); ++i)
                        in = PackResolver::Distance(p.pos, pullAt[i]) <= toTank;
                    a.atSec.push_back(p.atSec);
                    a.inside.push_back(in);
                    any = any || in;
                }
                if (any)
                    out.patrols.push_back(std::move(a));
            }

            // Comes by only after the fight when pulled now and later: not part of this pull.
            out.now = SimulateStart(input, out.patrols, 0.0f, out.fightSec);
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
            out.considered = true;
            out.bearableNow = FightLoad::Bearable(out.now);
            out.atStart = out.now;
            out.bearableAtStart = out.bearableNow;
            if (out.bearableNow)
                return out;

            // The earliest bearable start, else the best one.
            float bestSec = 0.0f;
            FightLoad::Result best = out.now;
            for (float s = PatrolTracker::SAMPLE_STEP_SEC; s <= waitBudgetSec + 0.001f; s += PatrolTracker::SAMPLE_STEP_SEC)
            {
                FightLoad::Result const r = SimulateStart(input, out.patrols, s, out.fightSec);
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
            std::string const when = s.mainArriveNow <= 0.0f ? name + " is next to the pull"
                : Format("%s joins in about %.0f s", name.c_str(), s.mainArriveNow);
            return when + Format(" (%s, within %.0f yd)", PatrolTracker::SourceName(main->source), main->clearance);
        }

        // What the patrol check decided, for the report and the notes. `waiting`: "The pull
        // waits" (planned) or "Waiting" (held).
        std::string PatrolVerdict(PatrolStart const& s, char const* waiting)
        {
            std::string const head = "Patrol " + DescribePatrol(s) + ". ";
            if (s.bearableNow)
                return head + "The party can take it, pulling without waiting: " + FightLoad::Describe(s.now) + ".";
            if (s.startSec > 0.0f)
                return head + "Pulled now: " + FightLoad::Describe(s.now) + Format(". %s about %.0f s", waiting,
                    std::ceil(s.startSec)) + (s.bearableAtStart ? ": " : ", which makes it easier: ") +
                    FightLoad::Describe(s.atStart) + ".";
            return head + "Pulled now: " + FightLoad::Describe(s.now) +
                Format(". Waiting up to %u s does not make it easier.", PullPlanner::PATROL_WAIT_MAX_MS / 1000);
        }
    }
}
