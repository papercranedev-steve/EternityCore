#include "Commander/Coordination/Duties/TargetAssigner.h"
#include "Commander/Coordination/Duties/TargetRules.h"
#include "Commander/Coordination/Duties/FocusAssigner.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/WorldModel.h"
#include "Commander/Perception/Values/SwitchCost.h"
#include "Commander/Perception/Tanks.h"
#include "Commander/Perception/Values/RestValue.h"
#include "Commander/CommanderMgr.h"
#include "Timer.h"
#include <algorithm>

namespace Commander
{
    // ---- TargetContext ----

    TargetContext::TargetContext(WorldModel const& w, TeamPlan const& p)
        : world(w), plan(p), tickSec(sCommanderMgr.GetBotUpdateInterval() / 1000.0f), tankBot(false)
    {
        for (PartyMemberFacts const* t : Tanks::Alive(world, true))
            tankBot = tankBot || t->CanAct();

        for (EnemyFacts const& e : world.enemies)
            if (!e.onAllyOnly)
                candidates.push_back(&e);
        for (EnemyFacts const& e : world.targeted)
            candidates.push_back(&e);

        // Controls handed out this allocation that break on damage: hitting the enemy before the
        // controller casts breaks it (or makes the cast pointless).
        for (DutyResult const& r : plan.duties)
        {
            if (r.kind != DutyKind::CrowdControl || r.primary.IsEmpty())
                continue;
            PartyMemberFacts const* controller = world.FindMember(r.primary);
            if (!controller)
                continue;
            for (ControlAbility const& c : controller->controls)
                if (c.spellId == r.primarySpellId && c.breaksOnDamage && c.radius <= 0.0f)
                    m_pendingControls.push_back({ r.target, r.primary });
        }
    }

    EnemyFacts const* TargetContext::Find(ObjectGuid guid) const
    {
        if (guid.IsEmpty())
            return nullptr;
        for (EnemyFacts const* e : candidates)
            if (e->guid == guid)
                return e;
        return nullptr;
    }

    bool TargetContext::Hittable(EnemyFacts const& e, PartyMemberFacts const& bot) const
    {
        if (!e.acting || !e.health || e.onAllyOnly)
            return false;
        if (e.control.spellId && e.control.breaksOnDamage &&
            !plan.controlEnd.Breaks(bot.guid, e.guid, world.refreshedAtMs))
            return false;
        for (Pending const& p : m_pendingControls)
            if (p.enemy == e.guid && p.controller != bot.guid)
                return false;
        // Patrol path (unless it is this bot's intercept), the player's "leave it alone".
        return plan.MayEngage(bot.guid, e.guid);
    }

    ObjectGuid TargetContext::InterceptOf(ObjectGuid bot) const
    {
        for (DutyResult const& r : plan.duties)
            if (r.kind == DutyKind::Intercept && r.primary == bot)
                return r.target;
        return ObjectGuid();
    }

    bool TargetContext::InterceptUrgent(ObjectGuid bot) const
    {
        for (DutyResult const& r : plan.duties)
            if (r.kind == DutyKind::Intercept && r.primary == bot)
                return (r.detail & FocusAssigner::DETAIL_URGENT) != 0;
        return false;
    }

    float TargetContext::InterceptPoints(ObjectGuid bot) const
    {
        for (DutyResult const& r : plan.duties)
            if (r.kind == DutyKind::Intercept && r.primary == bot)
                return r.points;
        return 0.0f;
    }

    FocusScore const& TargetContext::Score(EnemyFacts const& e) const
    {
        for (auto const& s : m_scores)
            if (s.first == e.guid)
                return s.second;
        m_scores.emplace_back(e.guid, FocusAssigner::Evaluate(world, e));
        return m_scores.back().second;
    }

    float TargetContext::Share() const
    {
        return 1.0f / float(std::max(1u, world.rates.damageDealers));
    }

    // ---- TargetAssigner ----

    namespace
    {
        void ExpireOrders(WorldModel const& world, TeamPlan& plan)
        {
            PlayerOrders& orders = plan.orders;
            if (!orders.attack.IsEmpty())
            {
                EnemyFacts const* e = world.FindEnemy(orders.attack);
                if (!e)
                    e = world.FindTargeted(orders.attack);
                bool const gone = !e || !e->health;
                if (gone && WorldTimer::getMSTimeDiff(orders.attackSinceMs, world.refreshedAtMs) >= TargetAssigner::ORDER_GRACE_MS)
                    orders.attack.Clear();
            }
            if (!orders.leaveAlone.empty() && !world.HasPartyEnemy() && world.targeted.empty())
                orders.leaveAlone.clear();
        }

        // Moved from FocusAssigner::Apply (2026-11-07): the focus is chosen where the targets are.
        void ChooseFocus(WorldModel const& world, std::vector<DutyResult> const& results, TeamPlan& plan)
        {
            // Intercepted enemies are one bot's job; the party focus stays where it is (scenario 8:
            // a skull on the bomb would pull the tank and the melee over too). An enemy given a
            // control this allocation is not the focus either: it is about to leave the fight.
            auto const excluded = [&results](ObjectGuid guid)
            {
                return std::any_of(results.begin(), results.end(), [guid](DutyResult const& r)
                {
                    return (r.kind == DutyKind::Intercept || r.kind == DutyKind::CrowdControl) &&
                           !r.primary.IsEmpty() && r.target == guid;
                });
            };
            auto const eligible = [&](EnemyFacts const& e)
            {
                // Standing in a patrol's path: the party stays off it (PatrolAssigner.h).
                // (party-wide: no bot's intercept exception)
                // A melee straggler on its way is the tank's (Stragglers.h).
                // An off-tank's enemy is killed last (OffTank.h).
                return FocusAssigner::IsEligible(world, e) && !excluded(e.guid) && plan.MayEngage(ObjectGuid(), e.guid) &&
                       !plan.doctrine.Has(e.guid, STAMP_STRAGGLER | STAMP_OFF_TANK);
            };

            // Doctrine (TacticalDoctrine.h, OutlierTactic::KillFirst): the pile is left to area
            // spells, so the focus is an enemy outside it (a caster standing back) while there is
            // one. Not while they are lured to the pile (Lure).
            bool const outliersFirst = plan.doctrine.outliers == OutlierTactic::KillFirst &&
                std::any_of(world.enemies.begin(), world.enemies.end(), [&](EnemyFacts const& e)
                {
                    return plan.doctrine.Has(e.guid, STAMP_OUTLIER) && eligible(e);
                });
            // Doctrine KillNow (RoleStamps.h): a self-destruct the party kills before it blows is
            // the focus before anything else, fresh or not (it is fresh: it just spawned).
            // Above that, the charmer of a party member (AllyCharm.h): killing it frees the ally.
            auto const stamped = [&](EnemyFacts const& e, uint32 stamp)
            {
                return plan.doctrine.Has(e.guid, stamp) && e.acting && e.health && !excluded(e.guid) &&
                       plan.MayEngage(ObjectGuid(), e.guid);
            };
            bool const anyFreeAlly = std::any_of(world.enemies.begin(), world.enemies.end(),
                [&](EnemyFacts const& e) { return stamped(e, STAMP_FREE_ALLY); });
            uint32 const urgentStamp = anyFreeAlly ? uint32(STAMP_FREE_ALLY) : uint32(STAMP_KILL_NOW);
            auto const killNow = [&](EnemyFacts const& e) { return stamped(e, urgentStamp); };
            bool const anyKillNow = std::any_of(world.enemies.begin(), world.enemies.end(), killNow);
            // Doctrine kill order (RoleStamps.h, user 2026-11-10): healers, summoners and totems
            // first while one is eligible; the points pick among them.
            bool const killOrder = !anyKillNow &&
                std::any_of(world.enemies.begin(), world.enemies.end(), [&](EnemyFacts const& e)
                {
                    return plan.doctrine.Has(e.guid, STAMP_KILL_ORDER) && eligible(e);
                });
            auto const allowed = [&](EnemyFacts const& e)
            {
                if (anyKillNow)
                    return killNow(e);
                if (killOrder)
                    return eligible(e) && plan.doctrine.Has(e.guid, STAMP_KILL_ORDER);
                return eligible(e) && (!outliersFirst || plan.doctrine.Has(e.guid, STAMP_OUTLIER));
            };

            EnemyFacts const* best = nullptr;
            FocusScore bestScore;
            for (EnemyFacts const& e : world.enemies)
            {
                if (!allowed(e))
                    continue;
                FocusScore s = FocusAssigner::Evaluate(world, e);
                if (s.score <= 0.0f && !anyKillNow)
                    continue;
                if (!best || s.score > bestScore.score)
                {
                    best = &e;
                    bestScore = std::move(s);
                }
            }

            if (!best)
            {
                plan.focus = FocusTarget();
                return;
            }

            // Sticky: switching costs every damage dealer a target change, and scores move with
            // health and victims every refresh.
            if (!plan.focus.enemy.IsEmpty() && plan.focus.enemy != best->guid)
            {
                EnemyFacts const* previous = world.FindEnemy(plan.focus.enemy);
                if (previous && allowed(*previous))
                {
                    FocusScore const kept = FocusAssigner::Evaluate(world, *previous);
                    if (kept.score >= FocusAssigner::STICKY_SHARE * bestScore.score)
                    {
                        plan.focus.score = kept.score;
                        plan.focus.harmRate = kept.harmRate;
                        plan.focus.killSec = kept.killSec;
                        plan.focus.reason = kept.reason;
                        return;
                    }
                }
            }

            if (plan.focus.enemy != best->guid)
                plan.focus.sinceMs = world.refreshedAtMs;
            plan.focus.enemy = best->guid;
            plan.focus.score = bestScore.score;
            plan.focus.harmRate = bestScore.harmRate;
            plan.focus.killSec = bestScore.killSec;
            plan.focus.reason = std::move(bestScore.reason);
            if (anyFreeAlly)
                plan.focus.reason += ", doctrine: kill it to free the mind controlled ally";
            else if (anyKillNow)
                plan.focus.reason += ", doctrine: kill it before it blows up";
            else if (killOrder)
                plan.focus.reason += ", doctrine: kill order (healer, summoner, totem first)";
            else if (outliersFirst)
                plan.focus.reason += ", doctrine: outside the area pile first";
        }

        // Nobody gets a target (TargetAssigner.h).
        bool HeldOut(WorldModel const& world, TeamPlan const& plan, PartyMemberFacts const& m)
        {
            if (plan.retreat.active)
                return true;
            if (m.inCombat)
                return false;
            PullPhase const phase = plan.pull.phase;
            if (m.guid == plan.pull.tank)
                return phase == PullPhase::WaitReady || phase == PullPhase::Approach;
            if (phase == PullPhase::Approach || phase == PullPhase::Return || phase == PullPhase::Settle)
                return true;
            // Resting to full pays more than its part of the fight (RestBehavior, same value):
            // no target until it is done. Its first hit would put it in combat, no more drinking.
            // (Free rest, no fight on or planned: there is no target to give either.)
            return !Tanks::IsTank(m) && RestValue::Of(world, m, plan.pull.PlannedFight()).Pays();
        }

        // TargetDuty::suggestUntilMs (header): a valued switch to an enemy about to run away.
        uint32 SuggestUntil(WorldModel const& world, PartyMemberFacts const& m, EnemyFacts const& e, TargetDuty const& d)
        {
            if (d.forced || d.urgent || d.enemy == m.victim)
                return 0;
            FleeFacts const& f = e.flee;
            if (!f.predicted || f.fleeing || f.FocusSaves() <= 0.0f)
                return 0;
            float const reach = m.role == ROLE_RANGE_DPS ? DamageTargets::RANGED_REACH : DamageTargets::MELEE_REACH;
            float const slack = f.secToFlee - SwitchCost::Seconds(world, m, e, reach) - TargetAssigner::SUGGEST_MARGIN_SEC;
            if (slack <= 0.0f)
                return 0;
            return world.refreshedAtMs + uint32(slack * 1000.0f);
        }

        TargetPick Forced(EnemyFacts const* e, char const* reason)
        {
            TargetPick p;
            p.enemy = e;
            p.forced = true;
            p.reason = reason;
            return p;
        }

        TargetPick ChooseFor(TargetContext const& ctx, PartyMemberFacts const& m, EnemyFacts const* current)
        {
            TeamPlan const& plan = ctx.plan;
            bool const tank = Tanks::IsTank(m);

            if (EnemyFacts const* e = ctx.Find(ctx.InterceptOf(m.guid)))
            {
                if (ctx.Hittable(*e, m))
                {
                    TargetPick p;
                    p.enemy = e;
                    p.urgent = ctx.InterceptUrgent(m.guid);
                    p.benefit = ctx.InterceptPoints(m.guid);
                    p.cost = SwitchCost::Of(ctx.world, m, *e,
                        m.role == ROLE_RANGE_DPS ? FocusAssigner::RANGED_REACH : FocusAssigner::MELEE_REACH);
                    p.reason = "intercept";
                    return p;
                }
            }
            if (!plan.orders.attack.IsEmpty() && (!plan.orders.attackTanksOnly || tank))
                if (EnemyFacts const* e = ctx.Find(plan.orders.attack))
                    if (ctx.Hittable(*e, m))
                        return Forced(e, "player order");
            if (plan.controlEnd.Breaks(m.guid, plan.controlEnd.enemy, ctx.world.refreshedAtMs))
                if (EnemyFacts const* e = ctx.Find(plan.controlEnd.enemy))
                    if (ctx.Hittable(*e, m))
                        return Forced(e, plan.controlEnd.why);
            if (EnemyFacts const* e = ctx.Find(m.focusMarkEnemy))
                if (ctx.Hittable(*e, m))
                    return Forced(e, "player focus mark");

            if (tank)
                return TankTargets::Choose(ctx, m, current);
            return DamageTargets::Choose(ctx, m, current);
        }
    }

    void TargetAssigner::Distribute(WorldModel const& world, TeamPlan& plan)
    {
        std::vector<TargetDuty> targets;
        {
            TargetContext const ctx(world, plan);
            for (PartyMemberFacts const& m : world.party)
            {
                if (!m.isBot || m.isPet || !m.CanAct() || m.role == ROLE_HEALER || m.role == ROLE_INVALID)
                    continue;
                if (HeldOut(world, plan, m))
                    continue;
                EnemyFacts const* current = ctx.Find(m.victim);
                if (current && !ctx.Hittable(*current, m))
                    current = nullptr;
                TargetPick const pick = ChooseFor(ctx, m, current);
                if (!pick.enemy)
                    continue;

                TargetDuty d;
                d.bot = m.guid;
                d.enemy = pick.enemy->guid;
                bool const already = d.enemy == m.victim;
                d.benefit = already ? 0.0f : pick.benefit;
                d.cost = already ? 0.0f : pick.cost;
                d.urgent = pick.urgent;
                d.forced = pick.forced;
                d.reason = pick.reason;
                TargetDuty const* previous = plan.FindTargetFor(m.guid);
                d.sinceMs = previous && previous->enemy == d.enemy ? previous->sinceMs : world.refreshedAtMs;
                d.suggestUntilMs = SuggestUntil(world, m, *pick.enemy, d);
                targets.push_back(d);
            }
        }
        plan.targets = std::move(targets);
    }

    void TargetAssigner::Apply(WorldModel const& world, std::vector<DutyResult> const& results, TeamPlan& plan) const
    {
        ExpireOrders(world, plan);
        ChooseFocus(world, results, plan);
        Distribute(world, plan);
    }

    void TargetAssigner::Clear(TeamPlan& plan) const
    {
        plan.targets.clear();
        plan.focus = FocusTarget();
    }
}
