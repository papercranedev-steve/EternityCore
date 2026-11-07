#include "Commander/Coordination/TargetRules.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/LooseEnemy.h"
#include "Commander/Perception/PackResolver.h"
#include "Commander/Perception/RouteRisk.h"
#include "Commander/Perception/SwitchCost.h"
#include "Commander/Perception/Tanks.h"
#include "Commander/Perception/ThreatView.h"
#include "Commander/Perception/ValueModel.h"
#include <algorithm>

// The tank's next enemy: TargetRules.h "Tank".
namespace Commander
{
namespace TankTargets
{
    namespace
    {
        float Net(TargetPick const& p) { return p.benefit - p.cost; }

        // 1. The most valuable loose enemy to win back with threat (the current target included).
        TargetPick BestLoose(TargetContext const& ctx, PartyMemberFacts const& tank, EnemyFacts const* current)
        {
            WorldModel const& world = ctx.world;
            float const tankRate = LooseEnemy::TankRate(world, tank.guid);
            TargetPick best;
            for (EnemyFacts const* e : ctx.candidates)
            {
                if (e->victim.IsEmpty() || e->victim == tank.guid || !ctx.Hittable(*e, tank))
                    continue;
                // On an ally (tank-like or not): it fights it on purpose (combat scenario 9).
                if (world.FindAlly(e->victim))
                    continue;
                PartyMemberFacts const* victim = world.FindMember(e->victim);
                if (!victim || victim->charmed || Tanks::IsTank(*victim))
                    continue;
                // Only this fight (a teleported member's room, combat scenario 10), and with several
                // tank bots the nearest one takes it.
                float const distance = PackResolver::Distance(tank.pos, e->pos);
                if (distance > TANK_REACH || Tanks::CloserBot(world, tank.guid, e->pos, distance, TANK_SHARE_MARGIN))
                    continue;
                bool const isCurrent = e == current;
                if (!isCurrent && distance > MELEE_REACH + e->boundingRadius &&
                    RouteRisk::Blocked(world, tank, *e))
                    continue;

                LooseValue const v = LooseEnemy::WinBack(world, *e, *victim, tank, tankRate, distance);
                TargetPick p;
                p.enemy = e;
                p.benefit = (v.lossNothing - v.lossThreat) * LooseEnemy::Mult(*e, nullptr);
                p.cost = isCurrent ? 0.0f : SwitchCost::Of(world, tank, *e, MELEE_REACH);
                p.urgent = v.deadly;
                p.reason = "win back a loose enemy";
                if (p.benefit <= p.cost)
                    continue;
                if (!best.enemy || Net(p) > Net(best))
                    best = p;
            }
            return best;
        }

        // 2. An enemy on the tank with a lead too thin for the next heal (fresh adds at 0 threat,
        // VanCleef's guards in scenario 6), while the current one is safe enough to leave.
        TargetPick TopUp(TargetContext const& ctx, PartyMemberFacts const& tank, EnemyFacts const* current)
        {
            WorldModel const& world = ctx.world;
            float currentRatio = ThreatView::NO_LIMIT;
            if (current && current->victim == tank.guid)
                currentRatio = ThreatView::VictimLead(*current) / std::max(1.0f, current->victimThreat);
            if (currentRatio <= LEAD_KEEP)
                return TargetPick();

            EnemyFacts const* weakest = nullptr;
            ObjectGuid runnerUp;
            float weakestRatio = ThreatView::NO_LIMIT;
            for (EnemyFacts const* e : ctx.candidates)
            {
                if (e == current || e->victim != tank.guid || !ctx.Hittable(*e, tank))
                    continue;
                ObjectGuid closest;
                float const ratio = ThreatView::VictimLead(*e, &closest) / std::max(1.0f, e->victimThreat);
                if (ratio < weakestRatio)
                {
                    weakestRatio = ratio;
                    weakest = e;
                    runnerUp = closest;
                }
            }
            if (!weakest || weakestRatio >= LEAD_SWITCH)
                return TargetPick();
            PartyMemberFacts const* runner = runnerUp.IsEmpty() ? nullptr : world.FindMember(runnerUp);
            if (!runner || runner->isPet)
                return TargetPick();

            TargetPick p;
            p.enemy = weakest;
            p.benefit = LooseEnemy::Loss(world, *weakest, *runner, tank, RECLAIM_SEC) * FRAGILE_WEIGHT;
            p.cost = SwitchCost::Of(world, tank, *weakest, MELEE_REACH);
            p.reason = "top up smallest lead";
            return p.benefit > p.cost ? p : TargetPick();
        }

        // 4. No target yet.
        TargetPick First(TargetContext const& ctx, PartyMemberFacts const& tank)
        {
            WorldModel const& world = ctx.world;
            auto const usable = [&](EnemyFacts const* e)
            {
                if (!e || !ctx.Hittable(*e, tank))
                    return false;
                float const distance = PackResolver::Distance(tank.pos, e->pos);
                return distance <= JOIN_FIGHT_YD &&
                       (distance <= MELEE_REACH + e->boundingRadius || !RouteRisk::Blocked(world, tank, *e));
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
        TargetPick loose = BestLoose(ctx, tank, current);
        if (loose.enemy)
        {
            if (loose.enemy == current)
                loose.benefit = loose.cost = 0.0f;
            return loose;
        }
        TargetPick const topUp = TopUp(ctx, tank, current);
        if (topUp.enemy)
            return topUp;
        if (current)
        {
            TargetPick keep;
            keep.enemy = current;
            keep.reason = "keep";
            return keep;
        }
        return First(ctx, tank);
    }
}
}
