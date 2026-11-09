// Line of sight pull and corner search (PullLosSearch.h, PullPlanner::SearchLosBreak).
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
#include "Commander/Perception/Spatial/ShotSpot.h"
#include "Commander/Perception/Tanks.h"
#include "Commander/Perception/Profiles/ProfileStore.h"
#include "Commander/Perception/Values/ValueModel.h"
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
        // Line of sight pull: pulled enemies standing back casting at the tank (map thread).
        // "Standing back": melee off (a CF_MAIN_RANGED_SPELL cast turns it off, scripts may too),
        // or a main ranged spell in its profile and not moving (between two casts).
        std::vector<Creature*> CastersStandingBack(WorldModel const& world, PullPlan const& pull, Map* map,
                                                   Position3 const& tankAt)
        {
            std::vector<Creature*> casters;
            for (EnemyFacts const& e : world.enemies)
            {
                if (e.onAllyOnly || !e.acting || e.crowdControlled || e.victim != pull.tank || IsUnexpected(e, pull))
                    continue;
                if (PackResolver::Distance(e.pos, tankAt) <= PullPlanner::LOS_CASTER_MIN_DISTANCE)
                    continue;
                Creature* c = map->GetCreature(e.guid);
                if (!c || !c->IsAlive())
                    continue;
                if (e.meleeEnabled && !(IsRangedCaster(c) && !c->IsMoving()))
                    continue;
                casters.push_back(c);
                if (casters.size() >= PullPlanner::LOS_MAX_CASTERS)
                    break;
            }
            return casters;
        }

        // Corner (PullPlan::corner): the pulled enemies coming for the tank that are not next
        // to it yet, the ones that stand back and cast first, then the farthest. The point hides
        // the tank from them, so they walk round the corner (a caster stops casting and comes,
        // the others bunch up there).
        std::vector<Creature*> CornerTargets(WorldModel const& world, PullPlan const& pull, Map* map,
                                             Position3 const& tankAt)
        {
            struct Candidate
            {
                Creature* creature;
                bool caster;
                float distance;
            };
            std::vector<Candidate> found;
            for (EnemyFacts const& e : world.enemies)
            {
                if (e.onAllyOnly || !e.acting || e.crowdControlled || e.victim != pull.tank || IsUnexpected(e, pull))
                    continue;
                float const d = PackResolver::Distance(e.pos, tankAt);
                if (d <= PullPlanner::LOS_CASTER_MIN_DISTANCE)
                    continue;
                Creature* c = map->GetCreature(e.guid);
                if (!c || !c->IsAlive())
                    continue;
                found.push_back({ c, IsRangedCaster(c), d });
            }
            std::sort(found.begin(), found.end(), [](Candidate const& a, Candidate const& b)
            {
                if (a.caster != b.caster)
                    return a.caster;
                return a.distance > b.distance;
            });
            std::vector<Creature*> out;
            for (Candidate const& c : found)
            {
                out.push_back(c.creature);
                if (out.size() >= PullPlanner::LOS_MAX_CASTERS)
                    break;
            }
            return out;
        }

        // Moving there takes the tank towards an idle hostile creature that is not in the pull.
        bool TowardsOtherCreature(WorldModel const& world, PullPlan const& pull, Position3 const& point,
                                  Position3 const& center)
        {
            for (PackFacts const& p : world.packs)
            {
                for (PackCreature const& c : p.members)
                {
                    if (!c.aggressive || c.noAggro || Contains(pull.expected, c.guid))
                        continue;
                    float const d = PackResolver::Distance(c.pos, point);
                    if (d < PullPlanner::LOS_OTHER_CREATURE_CLEARANCE && d < PackResolver::Distance(c.pos, center))
                        return true;
                }
            }
            return false;
        }

        // An aggressive idle patrol walks past `point` before the fight should end
        // (PatrolAssigner::InPath): a hiding point there brings the patrol in, and the patrol
        // module would only move the fight away again (strat-20261107-143623: the tank walked
        // to the hiding point, then 15 yd off the patrol's path, then to the patrol guard's
        // hiding point, in sight of the casters most of the time).
        bool InPatrolPath(WorldModel const& world, Position3 const& point)
        {
            float const remaining = Value::FightRemainingSec(world);
            for (PatrolFacts const& p : world.patrols)
                if (p.aggressive && !p.noAggro && !p.tappedByOther && !p.track.empty() &&
                    PatrolAssigner::InPath(p, point, remaining))
                    return true;
            return false;
        }
    }

    namespace PullDetail
    {
        // Pure (PullAssigner). The break ends when every caster came within LOS_CASTER_ARRIVED of
        // the tank or cannot come (dead, out of the fight, crowd controlled), turned to someone
        // else (hiding the tank does nothing then; the threat module takes it back), the fight
        // is over, or after LOS_BREAK_TIMEOUT_MS. `timedOut` says which.
        // A caster turning melee back on is not the end yet: it has only started to walk, and
        // the tank leaving the point would put it back in its sight (it stops again).
        bool LosBreakOver(WorldModel const& world, PullPlan const& pull, Position3 const& tankAt, uint32 nowMs,
                          bool& timedOut)
        {
            uint32 const timeoutMs = PullAssigner::LOS_BREAK_TIMEOUT_MS;
            float const arrived = PullAssigner::LOS_CASTER_ARRIVED;
            timedOut = false;
            if (!world.HasPartyEnemy())
                return true;
            if (WorldTimer::getMSTimeDiff(pull.losSinceMs, nowMs) > timeoutMs)
            {
                timedOut = true;
                return true;
            }
            for (ObjectGuid g : pull.losCasters)
            {
                EnemyFacts const* e = world.FindEnemy(g);
                if (e && e->acting && !e->crowdControlled && e->victim == pull.tank &&
                    PackResolver::Distance(e->pos, tankAt) > arrived)
                    return false;
            }
            return true;
        }
    }

    void PullPlanner::SearchLosBreak(TeamPlan& plan, WorldModel const& world, Map* map, uint32 nowMs)
    {
        PullPlan& pull = plan.pull;
        if (pull.phase != PullPhase::Settle || pull.losBreak != PullLosBreak::None)
            return;
        Unit* tank = map->GetUnit(pull.tank);
        if (!tank || !tank->IsAlive())
            return;
        Position3 const tankAt = PositionOf(tank);

        // No caster standing back yet: looked at again next refresh while Settle lasts. Facts
        // only, no line of sight checks until there is one. A corner fight (PullPlan::corner)
        // hides from the pulled enemies away from the tank from the start.
        std::vector<Creature*> casters = CastersStandingBack(world, pull, map, tankAt);
        bool const cornerFight = casters.empty() && pull.corner;
        if (cornerFight)
            casters = CornerTargets(world, pull, map, tankAt);
        if (casters.empty())
            return;

        // Around the fight spot when the pack was brought back, else where the tank fights.
        Position3 const center = pull.returnToSpot ? pull.spot : tankAt;

        // Shared search (LosBreak.h). No watchers: a healer bot that loses sight of the tank
        // finds itself a spot (HealerStanceBehavior, "tank out of reach").
        LosBreak::Search search;
        search.center = center;
        search.casters.assign(casters.begin(), casters.end());
        search.mover = tank;
        search.rejects = [&world, &pull, &center](Position3 const& p)
        {
            return TowardsOtherCreature(world, pull, p, center) || InPatrolPath(world, p);
        };

        Creature const* first = casters.front();
        Position3 point;
        LosBreak::PointQuality quality;
        bool const found = LosBreak::FindPoint(map, search, point, &quality);
        if (!found)
        {
            pull.losBreak = PullLosBreak::NoPoint;
            plan.notes.push_back(cornerFight
                ? Format("Corner: no spot out of %s's sight nearby, fighting here.", first->GetName())
                : Format("%s stands back and casts; no spot out of its sight nearby, fighting here.", first->GetName()));
            return;
        }
        pull.losBreak = PullLosBreak::Active;
        pull.losPoint = point;
        pull.losSinceMs = nowMs;
        pull.losCasters.clear();
        for (Creature const* c : casters)
            pull.losCasters.push_back(c->GetObjectGuid());
        // Fallback ranks of the search (LosBreak.h, "Ranking"): said so the review shows them.
        char const* fallback = !quality.groundOk && !quality.corner ? " (no corner, uneven ground: fallback)"
                             : !quality.groundOk ? " (uneven ground: fallback)"
                             : !quality.corner ? " (no corner, they walk straight to it: fallback)" : "";
        plan.notes.push_back(cornerFight
            ? Format("Corner: %s steps out of %s's sight (%.0f yd) so the pack comes round to it%s.",
                tank->GetName(), first->GetName(), PackResolver::Distance(tankAt, point), fallback)
            : Format("%s stands back and casts: %s steps out of its sight (%.0f yd) so it comes closer%s.",
                first->GetName(), tank->GetName(), PackResolver::Distance(tankAt, point), fallback));

        // Healer bots hide too, behind the tank as seen from the casters (LosHealerSpot.h).
        pull.losHiders.clear();
        for (PartyMemberFacts const& m : world.party)
        {
            if (!m.isBot || m.isPet || !m.alive || !m.CanAct() || m.role != ROLE_HEALER || m.guid == pull.tank)
                continue;
            Player* healer = map->GetPlayer(m.guid);
            if (!healer)
                continue;
            PullHider hider;
            hider.bot = m.guid;
            if (LosHealerSpot::Find(map, healer, point, casters, search.rejects, hider.point))
            {
                pull.losHiders.push_back(hider);
                plan.notes.push_back(Format("%s hides behind %s (%.0f yd from it) so a caster it pulls walks past the tank.",
                    healer->GetName(), tank->GetName(), PackResolver::Distance(point, hider.point)));
            }
            else
                plan.notes.push_back(Format("%s: no spot out of the casters' sight behind %s, staying.",
                    healer->GetName(), tank->GetName()));
        }
    }
}
