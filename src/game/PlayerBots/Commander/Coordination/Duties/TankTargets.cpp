#include "Commander/Coordination/Duties/TargetRules.h"
#include "Commander/Coordination/Duties/RescueAssigner.h"
#include "Commander/Coordination/Doctrine/TacticalDoctrine.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/Spatial/PackResolver.h"
#include "Commander/Perception/Spatial/RouteRisk.h"
#include "Commander/Perception/Values/SwitchCost.h"
#include "Commander/Perception/Values/ThreatRisk.h"
#include "Commander/Perception/Forecast/ThreatGap.h"
#include "Commander/Perception/Values/ValueModel.h"
#include <algorithm>

// The tank's next enemy: TargetRules.h "Tank".
namespace Commander
{
namespace TankTargets
{
    namespace
    {
        // 1. The rescue the brain gave this tank (RescueAssigner: a loose enemy, team rescue).
        // Winning it back with threat: that enemy. A taunt: cast where the tank stands when in
        // range and sight (RescueBehavior); else the tank walks over to that enemy first.
        TargetPick FromRescue(TargetContext const& ctx, PartyMemberFacts const& tank)
        {
            WorldModel const& world = ctx.world;
            for (DutyResult const& r : ctx.plan.duties)
            {
                if (r.kind != DutyKind::Rescue || r.primary != tank.guid)
                    continue;
                EnemyFacts const* e = ctx.Find(r.target);
                if (!e || !ctx.Hittable(*e, tank))
                    return TargetPick();
                bool const urgent = (r.detail & RescueAssigner::DETAIL_URGENT) != 0;
                char const* reason = r.primarySpellId ? "rescue: walk into taunt range" : "rescue: win back a loose enemy";
                if (r.primarySpellId)
                {
                    float const distance = PackResolver::Distance(tank.pos, e->pos) - e->boundingRadius;
                    for (TauntAbility const& t : tank.taunts)
                        if (t.spellId == r.primarySpellId && distance <= t.maxRange && e->InSightOf(tank.guid))
                        {
                            // A waiting taunt (RescueAssigner::TauntWaitSec): it runs at its
                            // ranged victim, the tank runs after it to keep it in reach.
                            if (RescueAssigner::TauntWaitSec(world, ctx.plan, *e, tank, r.primarySpellId, urgent) <= 0.0f)
                                return TargetPick();
                            reason = "rescue: keep in taunt reach, taunt when it pays most";
                        }
                }
                TargetPick p;
                p.enemy = e;
                p.cost = e->guid == tank.victim ? 0.0f : SwitchCost::Of(world, tank, *e, MELEE_REACH);
                p.benefit = r.points + p.cost;
                p.urgent = urgent;
                p.reason = reason;
                return p;
            }
            return TargetPick();
        }

        // 2. Threat (user 2026-11-13, doc .kiro/docs/2026-11-13-tank-threat-priority.md 4): the
        // enemy on the tank at the pile where one global cooldown of the tank's threat takes off
        // the most risk (Perception/Values/ThreatRisk.h, with the area spell the doctrine counts
        // on), net of the switch. The current target pays no switch; a switch has to beat it.
        // Nothing at risk: the current target (or none: First). Replaces the threat opening's
        // "fill one, then the next", the top-up and the Pick tactic's focus step (before
        // 2026-11-13: the tank sat 5 s on an enemy far ahead, strat-20261113-010348).
        TargetPick Threat(TargetContext const& ctx, PartyMemberFacts const& tank, EnemyFacts const* current)
        {
            WorldModel const& world = ctx.world;
            DoctrinePlan const& doctrine = ctx.plan.doctrine;
            float const horizon = doctrine.RunwayHorizon();
            float const gcdThreat = ThreatGap::FillRate(world, tank) * Value::GCD_SEC;
            auto const removed = [&](EnemyFacts const& e)
            {
                std::vector<PlannedThreat> const& planned = doctrine.AreaThreatOn(e.guid);
                return ThreatRisk::Of(world, e, tank.guid, planned, horizon) -
                       ThreatRisk::Of(world, e, tank.guid, planned, horizon, gcdThreat);
            };
            auto const counts = [&](EnemyFacts const& e)
            {
                return ThreatRisk::Counts(e, tank.guid) && ctx.Hittable(e, tank) &&
                       PackResolver::Distance(tank.pos, e.pos) - e.boundingRadius <= TacticalDoctrine::CLUSTER_YD;
            };

            float const stay = current && counts(*current) ? removed(*current) : 0.0f;
            TargetPick best;
            float bestNet = stay;
            for (EnemyFacts const* e : ctx.candidates)
            {
                if (e == current || !counts(*e))
                    continue;
                float const benefit = removed(*e);
                if (benefit <= 0.0f)
                    continue;
                float const cost = SwitchCost::Of(world, tank, *e, MELEE_REACH);
                if (benefit - cost > bestNet)
                {
                    bestNet = benefit - cost;
                    best.enemy = e;
                    best.benefit = benefit;
                    best.cost = cost;
                    best.reason = "threat: takes off the most risk";
                }
            }
            if (best.enemy || !current)
                return best;
            best.enemy = current;
            best.reason = stay > 0.0f ? "threat: its risk is the one to take off" : "keep: nothing at risk";
            return best;
        }

        // 3. No target yet.
        TargetPick First(TargetContext const& ctx, PartyMemberFacts const& tank)
        {
            WorldModel const& world = ctx.world;
            auto const usable = [&](EnemyFacts const* e)
            {
                if (!e || !ctx.Hittable(*e, tank))
                    return false;
                // An off-tank holds it (doctrine OffTank.h): not the tank's.
                if (ctx.plan.doctrine.Has(e->guid, STAMP_OFF_TANK))
                    return false;
                float const distance = PackResolver::Distance(tank.pos, e->pos);
                // An enemy already hitting one of us: the hard line (RouteFact::safeTight) is enough
                // (stockade-20261115-010336: the tank stood in the corridor 14 s, every walk into
                // the cell blocked by the opposite cell's comfort margin, while the rogue went in).
                bool const tight = !e->victim.IsEmpty();
                return distance <= JOIN_FIGHT_YD &&
                       (distance <= MELEE_REACH + e->boundingRadius || !RouteRisk::Blocked(world, tank, *e, tight));
            };
            auto const pick = [&](EnemyFacts const* e, char const* reason)
            {
                TargetPick p;
                p.enemy = e;
                FocusScore const& f = ctx.Score(*e);
                // Idle until now, so switching loses nothing; at least one global cooldown of its own output.
                p.benefit = std::max(f.harmRate * f.killSec * ctx.Share(),
                                     Value::ContributionPerSec(world, tank) * Value::GCD_SEC);
                p.reason = reason;
                return p;
            };

            EnemyFacts const* e = ctx.Find(ctx.plan.pull.target);
            if (usable(e))
                return pick(e, "pull target");
            if (PartyMemberFacts const* leader = world.FindMember(world.leader))
                if (usable(e = ctx.Find(leader->victim)))
                    return pick(e, "what the leader attacks");
            for (EnemyFacts const& t : world.targeted)
                if (usable(&t))
                    return pick(&t, "what the party attacks");

            EnemyFacts const* nearest = nullptr;
            float nearestDistance = 0.0f;
            bool nearestOnMe = false;
            for (EnemyFacts const* c : ctx.candidates)
            {
                if (!usable(c))
                    continue;
                bool const onMe = c->victim == tank.guid;
                float const distance = PackResolver::Distance(tank.pos, c->pos);
                if (!nearest || (onMe && !nearestOnMe) || (onMe == nearestOnMe && distance < nearestDistance))
                {
                    nearest = c;
                    nearestDistance = distance;
                    nearestOnMe = onMe;
                }
            }
            return nearest ? pick(nearest, nearestOnMe ? "hitting me" : "nearest enemy") : TargetPick();
        }
    }

    TargetPick Choose(TargetContext const& ctx, PartyMemberFacts const& tank, EnemyFacts const* current)
    {
        // On an off-tank's enemy (doctrine OffTank.h): any other is taken as from nothing.
        if (current && ctx.plan.doctrine.Has(current->guid, STAMP_OFF_TANK))
            current = nullptr;
        TargetPick loose = FromRescue(ctx, tank);
        if (loose.enemy)
        {
            if (loose.enemy == current)
                loose.benefit = loose.cost = 0.0f;
            return loose;
        }
        TargetPick const threat = Threat(ctx, tank, current);
        if (threat.enemy)
            return threat;
        return First(ctx, tank);
    }
}
}
