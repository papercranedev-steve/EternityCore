#include "Commander/Coordination/CrowdControlAssigner.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/ControlValue.h"
#include "Commander/Perception/FightLoad.h"
#include "Commander/Perception/PackResolver.h"
#include "Commander/Perception/WorldModel.h"
#include <algorithm>

namespace Commander
{
    namespace
    {
        using CC = CrowdControlAssigner;

        // Still held for longer than it takes to cast `a` again (plus the lead): no need yet.
        bool HeldLongEnough(EnemyFacts const& e, ControlAbility const& a)
        {
            return e.control.spellId && e.control.remainingMs > a.castMs + uint32(CC::RECAST_LEAD_SEC * 1000.0f);
        }

        bool WasGiven(TeamPlan const& plan, ObjectGuid bot, ObjectGuid enemy)
        {
            for (ControlDuty const& d : plan.controls)
                if (d.bot == bot && d.enemy == enemy)
                    return true;
            return false;
        }

        uint8 Detail(ControlAbility const& a)
        {
            return uint8((a.radius > 0.0f ? CC::DETAIL_AREA : 0) | (a.breaksOnDamage ? CC::DETAIL_BREAKS : 0));
        }

        ControlAbility const* FindAbility(WorldModel const& world, ObjectGuid bot, uint32 spellId)
        {
            if (PartyMemberFacts const* m = world.FindMember(bot))
                for (ControlAbility const& a : m->controls)
                    if (a.spellId == spellId)
                        return &a;
            return nullptr;
        }
    }

    void CrowdControlAssigner::Clear(TeamPlan& plan) const
    {
        plan.controls.clear();
        plan.protectedEnemies.clear();
    }

    void CrowdControlAssigner::Offer(WorldModel const& world, TeamPlan const& plan, std::vector<DutyNeed>& out) const
    {
        if (!world.HasPartyEnemy())
            return;
        FightLoad::Input fight;
        if (!FightLoad::FromWorld(world, fight))
            return;

        std::vector<DutyNeed> perEnemy;     // single target needs, one per enemy
        auto needFor = [&perEnemy](ObjectGuid enemy) -> DutyNeed&
        {
            for (DutyNeed& n : perEnemy)
                if (n.target == enemy)
                    return n;
            perEnemy.emplace_back();
            DutyNeed& n = perEnemy.back();
            n.kind = DutyKind::CrowdControl;
            n.target = enemy;
            n.now = true;
            n.reason = "control";
            return n;
        };

        // Opening control of the pull (PullControl.h): once its target is in the fight and
        // not controlled yet, the planned controller casts it, valued as planned. The target
        // is left out of the offers below meanwhile (one need per enemy).
        PullControlPlan const& opening = plan.pull.control;
        ObjectGuid openingTarget;
        if (opening.Active() && plan.pull.phase != PullPhase::None)
        {
            EnemyFacts const* e = world.FindEnemy(opening.target);
            PartyMemberFacts const* b = world.FindMember(opening.bot);
            if (e && e->acting && !e->control.spellId && b && b->CanAct())
            {
                openingTarget = e->guid;
                ControlAbility const* a = FindAbility(world, b->guid, opening.spellId);
                DutyNeed need;
                need.kind = DutyKind::CrowdControl;
                need.target = e->guid;
                need.now = true;
                need.points = opening.points;
                need.detail = a ? Detail(*a) : uint8(0);
                need.reason = "opening control";
                DutyCandidate c;
                c.bot = b->guid;
                c.spellId = opening.spellId;
                c.rank = opening.points;
                need.candidates.push_back(c);
                out.push_back(std::move(need));
            }
        }

        for (PartyMemberFacts const& b : world.party)
        {
            if (!b.isBot || b.isPet || !b.CanAct())
                continue;
            for (ControlAbility const& a : b.controls)
            {
                if (!a.ready || a.peacefulOnly)
                    continue;

                if (a.radius > 0.0f)
                {
                    std::vector<EnemyFacts const*> const targets = ControlValue::Targets(world, b, a, nullptr);
                    if (targets.empty() || std::all_of(targets.begin(), targets.end(),
                            [&a](EnemyFacts const* e) { return HeldLongEnough(*e, a); }))
                        continue;
                    ControlValue::Worth const w = ControlValue::Of(world, fight, b, a, targets, 0.0f);
                    if (w.Net() <= 0.0f)
                        continue;
                    // The nearest one: the target of a shout that needs one (Intimidating Shout).
                    EnemyFacts const* nearest = targets.front();
                    for (EnemyFacts const* e : targets)
                        if (PackResolver::Distance(b.pos, e->pos) < PackResolver::Distance(b.pos, nearest->pos))
                            nearest = e;
                    DutyNeed need;
                    need.kind = DutyKind::CrowdControl;
                    need.target = nearest->guid;
                    need.now = true;
                    need.points = w.Net();
                    need.detail = Detail(a);
                    need.reason = a.fear ? "area fear" : "area control";
                    DutyCandidate c;
                    c.bot = b.guid;
                    c.spellId = a.spellId;
                    c.rank = w.Net();
                    need.candidates.push_back(c);
                    out.push_back(std::move(need));
                    continue;
                }

                for (EnemyFacts const& e : world.enemies)
                {
                    if (e.guid == openingTarget || e.onAllyOnly || !e.acting || !e.health || HeldLongEnough(e, a) ||
                        !ControlValue::Affects(a, e.entry, e.creatureTypeMask))
                        continue;
                    if (PackResolver::Distance(b.pos, e.pos) - e.boundingRadius > a.maxRange + REACH_SLACK_YD)
                        continue;
                    std::vector<EnemyFacts const*> const targets = { &e };
                    float const aoe = a.breaksOnDamage ? ControlValue::AreaLoss(world, e, ControlValue::ExpectedSec(world, a, e)) : 0.0f;
                    ControlValue::Worth const w = ControlValue::Of(world, fight, b, a, targets, aoe);
                    if (w.Net() <= 0.0f)
                        continue;
                    DutyNeed& need = needFor(e.guid);
                    DutyCandidate c;
                    c.bot = b.guid;
                    c.spellId = a.spellId;
                    c.rank = w.Net();
                    if (e.control.caster == b.guid || WasGiven(plan, b.guid, e.guid))
                        c.rank += STICKY_POINTS;
                    need.candidates.push_back(c);
                    if (w.Net() > need.points)
                    {
                        need.points = w.Net();
                        need.detail = Detail(a);
                    }
                }
            }
        }
        for (DutyNeed& n : perEnemy)
            out.push_back(std::move(n));
    }

    void CrowdControlAssigner::Apply(WorldModel const& world, std::vector<DutyResult> const& results, TeamPlan& plan) const
    {
        std::vector<ControlDuty> duties;
        std::vector<ObjectGuid> takenByArea;
        for (DutyResult const& r : results)
        {
            if (r.kind != DutyKind::CrowdControl || r.primary.IsEmpty())
                continue;
            ControlDuty d;
            d.enemy = r.target;
            d.bot = r.primary;
            d.spellId = r.primarySpellId;
            d.points = r.points;
            // The chosen bot's own spell decides (the need's detail is the best candidate's).
            ControlAbility const* a = FindAbility(world, d.bot, d.spellId);
            d.area = a ? a->radius > 0.0f : (r.detail & DETAIL_AREA) != 0;
            d.breaksOnDamage = a ? a->breaksOnDamage : (r.detail & DETAIL_BREAKS) != 0;
            if (d.area && a)
                if (PartyMemberFacts const* m = world.FindMember(d.bot))
                    for (EnemyFacts const* e : ControlValue::Targets(world, *m, *a, nullptr))
                        takenByArea.push_back(e->guid);
            duties.push_back(d);
        }
        duties.erase(std::remove_if(duties.begin(), duties.end(), [&takenByArea](ControlDuty const& d)
        {
            return !d.area && std::find(takenByArea.begin(), takenByArea.end(), d.enemy) != takenByArea.end();
        }), duties.end());

        plan.protectedEnemies.clear();
        for (ControlDuty const& d : duties)
            if (d.breaksOnDamage && !d.area)
                plan.protectedEnemies.push_back(d.enemy);
        plan.controls = std::move(duties);
    }
}
