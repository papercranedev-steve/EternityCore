#include "Commander/Coordination/Duties/TargetRules.h"
#include "Commander/Coordination/Duties/RescueAssigner.h"
#include "Commander/Coordination/Doctrine/TacticalDoctrine.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/Values/LooseEnemy.h"
#include "Commander/Perception/Spatial/PackResolver.h"
#include "Commander/Perception/Spatial/RouteRisk.h"
#include "Commander/Perception/Values/SwitchCost.h"
#include "Commander/Perception/Tanks.h"
#include "Commander/Perception/Forecast/ThreatRunway.h"
#include "Commander/Perception/Forecast/ThreatView.h"
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
                if (r.primarySpellId)
                {
                    float const distance = PackResolver::Distance(tank.pos, e->pos) - e->boundingRadius;
                    for (TauntAbility const& t : tank.taunts)
                        if (t.spellId == r.primarySpellId && distance <= t.maxRange && e->InSightOf(tank.guid))
                            return TargetPick();
                }
                TargetPick p;
                p.enemy = e;
                p.cost = e->guid == tank.victim ? 0.0f : SwitchCost::Of(world, tank, *e, MELEE_REACH);
                p.benefit = r.points + p.cost;
                p.urgent = (r.detail & RescueAssigner::DETAIL_URGENT) != 0;
                p.reason = r.primarySpellId ? "rescue: walk into taunt range" : "rescue: win back a loose enemy";
                return p;
            }
            return TargetPick();
        }

        TargetPick Keep(EnemyFacts const* e, char const* reason)
        {
            TargetPick p;
            p.enemy = e;
            p.reason = reason;
            return p;
        }

        // 2. The threat opening's task (ThreatOpening.h): stay on the current enemy while it is
        // short, else switch to the one short the most. The switch is worth the time the waiting
        // party saves: waitPerSec x its gap / the tank's single target rate.
        TargetPick Opening(TargetContext const& ctx, PartyMemberFacts const& tank, EnemyFacts const* current)
        {
            ThreatOpeningPlan const& t = ctx.plan.threat;
            if (!t.active || t.tank != tank.guid || t.gaps.empty())
                return TargetPick();
            if (current && t.GapOf(current->guid) > 0.0f)
                return Keep(current, "opening: build threat on it");
            EnemyFacts const* best = nullptr;
            float bestGap = 0.0f;
            for (ThreatGapEntry const& g : t.gaps)
            {
                EnemyFacts const* e = ctx.Find(g.enemy);
                if (e && g.gap > bestGap && ctx.Hittable(*e, tank))
                {
                    best = e;
                    bestGap = g.gap;
                }
            }
            if (!best)
                return TargetPick();
            TargetPick p;
            p.enemy = best;
            p.benefit = t.waitPerSec * bestGap / t.fillRate;
            p.cost = SwitchCost::Of(ctx.world, tank, *best, MELEE_REACH);
            p.reason = "opening: the enemy short the most";
            return p;
        }

        // 3. Top up (user 2026-11-08: "多打一会", 2026-11-12: by the runway): the enemy on the
        // tank, at the pile, whose threat runway (Perception/Forecast/ThreatRunway.h, with the
        // area spell the doctrine counts on) ends soonest within the doctrine's horizon. The tank
        // stays on the current one while its runway is short and the shortest; a long runway on
        // the current one makes a switch cheap (round robin with satiation, doc
        // .kiro/docs/2026-11-11-aoe-threat.md 4.2). Before 2026-11-12: only an enemy whose lead
        // did not survive the healer's next heal, so the tank sat on one enemy far ahead while
        // the others thinned out; before 2026-11-08: lead / threat below 15%.
        TargetPick TopUp(TargetContext const& ctx, PartyMemberFacts const& tank, EnemyFacts const* current)
        {
            WorldModel const& world = ctx.world;
            DoctrinePlan const& doctrine = ctx.plan.doctrine;
            float const horizon = doctrine.RunwayHorizon();
            auto const runway = [&](EnemyFacts const& e)
            {
                return ThreatRunway::Of(world, e, tank.guid, doctrine.AreaThreatOn(e.guid), horizon);
            };

            float currentSec = horizon;
            if (current && current->victim == tank.guid)
                currentSec = std::min(horizon, runway(*current).sec);

            EnemyFacts const* weakest = nullptr;
            ThreatRunway::Result weakestRunway;
            weakestRunway.sec = horizon;
            for (EnemyFacts const* e : ctx.candidates)
            {
                if (e == current || e->victim != tank.guid || !ctx.Hittable(*e, tank) ||
                    PackResolver::Distance(tank.pos, e->pos) - e->boundingRadius > TacticalDoctrine::CLUSTER_YD)
                    continue;
                ThreatRunway::Result const r = runway(*e);
                if (r.sec < weakestRunway.sec)
                {
                    weakestRunway = r;
                    weakest = e;
                }
            }
            if (currentSec < horizon && (!weakest || currentSec <= weakestRunway.sec))
                return Keep(current, "top up: its runway is the shortest");
            if (!weakest)
                return TargetPick();
            // Who takes it when the runway ends, else a healer bot (the next heal puts it there).
            PartyMemberFacts const* runner = weakestRunway.taker.IsEmpty() ? nullptr : world.FindMember(weakestRunway.taker);
            if (!runner)
                for (PartyMemberFacts const& m : world.party)
                    if (m.isBot && !m.isPet && m.alive && m.role == ROLE_HEALER)
                    {
                        runner = &m;
                        break;
                    }
            if (!runner || runner->isPet)
                return currentSec < horizon ? Keep(current, "top up: its runway is short") : TargetPick();

            TargetPick p;
            p.enemy = weakest;
            p.benefit = LooseEnemy::ThinLoss(world, *weakest, *runner, tank, weakestRunway.sec, horizon);
            p.cost = SwitchCost::Of(world, tank, *weakest, MELEE_REACH);
            p.reason = "top up: the shortest threat runway";
            if (p.benefit > p.cost)
                return p;
            return currentSec < horizon ? Keep(current, "top up: its runway is short") : TargetPick();
        }

        // 4. The doctrine's job Focus (Pick tactic, TankJob): the main tank hits the party focus
        // too, when it is at the pile (TacticalDoctrine::CLUSTER_YD, so the tank does not leave
        // the enemies on it) and the switch pays: Smith's rule exchange as for the damage dealers
        // (DamageTargets), the tank's share of it, against SwitchCost.
        TargetPick Focus(TargetContext const& ctx, PartyMemberFacts const& tank, EnemyFacts const* current)
        {
            WorldModel const& world = ctx.world;
            if (ctx.plan.doctrine.tankJob != TankJob::Focus)
                return TargetPick();
            PartyMemberFacts const* main = Tanks::Main(world);
            if (!main || main->guid != tank.guid)
                return TargetPick();
            EnemyFacts const* f = ctx.Find(ctx.plan.focus.enemy);
            if (!f || f == current || !ctx.Hittable(*f, tank) ||
                PackResolver::Distance(tank.pos, f->pos) - f->boundingRadius > TacticalDoctrine::CLUSTER_YD ||
                RouteRisk::Blocked(world, tank, *f))
                return TargetPick();
            TargetPick p;
            p.enemy = f;
            p.cost = SwitchCost::Of(world, tank, *f, MELEE_REACH);
            FocusScore const& fs = ctx.Score(*f);
            if (current)
            {
                FocusScore const& cs = ctx.Score(*current);
                p.benefit = (fs.harmRate * cs.killSec - cs.harmRate * fs.killSec) * ctx.Share();
            }
            else
                p.benefit = std::max(fs.harmRate * fs.killSec * ctx.Share(),
                                     Value::ContributionPerSec(world, tank) * Value::GCD_SEC);
            p.reason = "doctrine: the tank hits the focus (pick)";
            return p.benefit > p.cost ? p : TargetPick();
        }

        // 5. No target yet.
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
        TargetPick loose = FromRescue(ctx, tank);
        if (loose.enemy)
        {
            if (loose.enemy == current)
                loose.benefit = loose.cost = 0.0f;
            return loose;
        }
        TargetPick const opening = Opening(ctx, tank, current);
        if (opening.enemy)
            return opening;
        TargetPick const topUp = TopUp(ctx, tank, current);
        if (topUp.enemy)
            return topUp;
        TargetPick const focus = Focus(ctx, tank, current);
        if (focus.enemy)
            return focus;
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
