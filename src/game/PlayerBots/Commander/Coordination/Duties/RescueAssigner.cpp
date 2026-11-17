#include "Commander/Coordination/Duties/RescueAssigner.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Coordination/Doctrine/OffTank.h"
#include "Commander/Perception/Forecast/TauntWindow.h"
#include "Commander/Perception/Values/LooseEnemy.h"
#include "Commander/Perception/Spatial/PackResolver.h"
#include "Commander/Perception/Values/RescueValue.h"
#include "Commander/Perception/Spatial/RouteRisk.h"
#include "Commander/Perception/Values/SurvivalValue.h"
#include "Commander/Perception/Tanks.h"
#include "Commander/Perception/WorldModel.h"
#include <algorithm>
#include <limits>

namespace Commander
{
    namespace
    {
        // Given to `bot` with `spellId` for `enemy` last refresh.
        bool WasGiven(TeamPlan const& plan, ObjectGuid bot, ObjectGuid enemy, uint32 spellId)
        {
            return std::any_of(plan.rescues.begin(), plan.rescues.end(), [&](RescueDuty const& d)
            {
                return d.bot == bot && d.enemy == enemy && d.spellId == spellId;
            });
        }
    }

    float RescueAssigner::TauntWaitSec(WorldModel const& world, TeamPlan const& plan, EnemyFacts const& e,
                                       PartyMemberFacts const& tank, uint32 spellId, bool urgent, float* gain)
    {
        if (gain)
            *gain = 0.0f;
        if (urgent || !spellId || !e.meleeEnabled || e.crowdControlled)
            return 0.0f;
        // Ranged damage dealers only (user 2026-11-15): on a melee or a healer every second is hits.
        PartyMemberFacts const* victim = RescueValue::LooseVictim(world, e);
        if (!victim || victim->isPet || victim->role != ROLE_RANGE_DPS || plan.area.CastBy(victim->guid))
            return 0.0f;
        auto const taunt = std::find_if(tank.taunts.begin(), tank.taunts.end(),
                                        [spellId](TauntAbility const& t) { return t.spellId == spellId; });
        if (taunt == tank.taunts.end() || !taunt->ready || taunt->holdSec > 0.0f || !e.InSightOf(tank.guid))
            return 0.0f;
        TauntWindow::Window const w = TauntWindow::Of(e, *victim, tank, *taunt);
        if (w.reachLeftSec <= 0.0f)
            return 0.0f;
        float const sec = std::min({ w.arriveSec, w.reachLeftSec, WAIT_MAX_SEC }) - WAIT_LEAD_SEC;
        if (sec < WAIT_MIN_SEC)
            return 0.0f;
        float const added = TauntWindow::ThreatGain(w, sec);
        if (added <= 0.0f || added < WAIT_MIN_GAIN_SHARE * w.threatNow)
            return 0.0f;
        if (gain)
            *gain = added;
        return sec;
    }

    void RescueAssigner::Offer(WorldModel const& world, TeamPlan const& plan, std::vector<DutyNeed>& out) const
    {
        if (!world.HasPartyEnemy())
            return;

        std::vector<PartyMemberFacts const*> tanks;
        for (PartyMemberFacts const& m : world.party)
            if (m.isBot && !m.isPet && m.CanAct() && Tanks::IsTank(m))
                tanks.push_back(&m);
        for (EnemyFacts const& e : world.enemies)
        {
            PartyMemberFacts const* victim = RescueValue::LooseVictim(world, e);
            if (!victim || !plan.MayEngage(ObjectGuid(), e.guid))
                continue;
            // Doctrine (OffTank.h): on the off-tank holding it, it is held.
            if (plan.doctrine.OffTankHolds(victim->guid, e.guid))
                continue;
            // Doctrine (RoleStamps.h): a self-destruct dies anyway, nobody takes it; NoTaunt (a
            // trap swarm, taunt immune, a totem): no single target taunt (winning it back with
            // threat, area threat: still).
            if (plan.doctrine.Has(e.guid, STAMP_SELF_DESTRUCT))
                continue;
            // Doctrine (Stragglers.h): standing back and not hurting its victim enough for the
            // tank's going: the damage dealers' (user 2026-11-13).
            if (plan.doctrine.Has(e.guid, STAMP_DPS_ONLY))
                continue;
            bool const noTaunt = plan.doctrine.Has(e.guid, STAMP_NO_TAUNT);
            float const enemyDps = RescueValue::NextTauntDps(world, e.guid);

            DutyNeed need;
            need.kind = DutyKind::Rescue;
            need.target = e.guid;
            need.now = true;
            need.blockedBy = { { DutyKeyKind::EnemyHandled, e.guid }, { DutyKeyKind::Saved, victim->guid } };
            need.marks = { { DutyKeyKind::EnemyHandled, e.guid }, { DutyKeyKind::LooseTaken, victim->guid } };
            need.reason = "loose enemy";
            // Doctrine HoldPile (Area tactic, TankJob): the tank protects the area caster (the
            // area phase of this refresh's situation step). Which way (taunt, threat, its own
            // threat drop) is still the points'; that it is done first is the doctrine's: Urgent.
            bool const protects = plan.doctrine.tankJob == TankJob::HoldPile &&
                                  plan.area.CastBy(victim->guid);
            if (protects)
            {
                need.detail |= DETAIL_URGENT;
                need.reason = "loose enemy on the area caster (doctrine: protect it)";
            }
            // The area phase counts on the tanks' taunts for the enemies it turns (AreaPhase,
            // this refresh's situation step): a taunt spent outside its area pays that.
            float const areaReserve = plan.area.TauntReserveOutside(e.guid);
            auto const add = [&](ObjectGuid bot, uint32 spellId, RescueValue::Option o)
            {
                if (spellId && bot != victim->guid)
                    o.cost += areaReserve;
                if (o.Net() <= 0.0f)
                    return;
                DutyCandidate c;
                c.bot = bot;
                c.spellId = spellId;
                c.rank = o.Net() + (WasGiven(plan, bot, e.guid, spellId) ? STICKY_POINTS : 0.0f);
                c.value = c.rank;
                c.valued = true;
                need.candidates.push_back(c);
                need.points = std::max(need.points, o.Net());
                if (o.urgent)
                    need.detail |= DETAIL_URGENT;
            };

            for (PartyMemberFacts const* tank : tanks)
            {
                float const distance = PackResolver::Distance(tank->pos, e.pos);
                if (distance > TANK_REACH)
                    continue;
                float const tankRate = LooseEnemy::TankRate(world, tank->guid);
                // On the hard line (RouteFact::safeTight, 2026-11-15, stockade-20261115-010336: the
                // player pulled a cell, the opposite cell's margin closed the door to the tank for
                // 14 s while the rogue walked in on the hard line and the player nearly died). A
                // member is being hit: the comfort margin is not worth that.
                bool const walkBlocked = tank->victim != e.guid && distance > MELEE_REACH + e.boundingRadius &&
                                         RouteRisk::Blocked(world, *tank, e, true);
                if (!walkBlocked)
                    add(tank->guid, 0, RescueValue::Threat(world, e, *victim, *tank, tankRate, distance));
                for (TauntAbility const& t : tank->taunts)
                {
                    if (noTaunt)
                        break;
                    if (walkBlocked && distance - e.boundingRadius > t.maxRange)
                        continue;
                    add(tank->guid, t.spellId, RescueValue::Taunt(world, e, *victim, *tank, t, tankRate, distance, enemyDps));
                }
            }

            // A free off-tank (OffTank.h, user 2026-11-15: the hunter's pet): no taunt, it wins the
            // enemy with threat; once the enemy hits it, the doctrine keeps it there.
            if (OffTank::MayTake(plan.doctrine, e))
                for (PartyMemberFacts const& h : world.party)
                {
                    if (h.guid == victim->guid || !OffTank::IsFree(world, plan.doctrine, h))
                        continue;
                    float const distance = PackResolver::Distance(h.pos, e.pos);
                    if (distance > TANK_REACH)
                        continue;
                    add(h.guid, 0, RescueValue::Threat(world, e, *victim, h, LooseEnemy::TankRate(world, h.guid), distance));
                }

            // The victim's own threat drops (Fade, Feint, Cower, Feign Death, Vanish): only now
            // that the enemy hits it (user 2026-11-14: not while it is merely close to the
            // holder's threat, that is ThreatRoom's stop / switch).
            // A bot's pet too (Cower, WorldModelBuilder's pet facts): its owner has it cast.
            if (victim->isBot || victim->isPet)
                for (ThreatDropAbility const& a : victim->threatDrops)
                    add(victim->guid, a.spellId, RescueValue::ThreatDrop(world, e, *victim, a));

            if (!need.candidates.empty())
                out.push_back(std::move(need));
        }

        // 2. Members about to die (Perception/Values/SurvivalValue.h): one spell to keep each alive.
        for (PartyMemberFacts const& m : world.party)
        {
            // Pets: only one that holds enemies for the party (an off-tank, user 2026-11-15:
            // the hunter's pet is half a tank); another pet's death costs a revive, not the fight.
            if ((m.isPet && !m.canOffTank) || !m.alive)
                continue;
            SurvivalValue::Danger const danger = SurvivalValue::Of(world, m);
            if (!danger.InDanger())
                continue;
            DutyNeed need;
            need.kind = DutyKind::Save;
            need.target = m.guid;
            need.now = true;
            need.detail = DETAIL_URGENT;
            need.reason = "about to die";
            need.blockedBy = { { DutyKeyKind::LooseTaken, m.guid } };
            need.marks = { { DutyKeyKind::Saved, m.guid } };
            std::vector<bool> consumable;   // per candidate
            for (PartyMemberFacts const& b : world.party)
            {
                // Bots, and bots' pets (Shell Shield on itself: the owner has it cast).
                if (!b.isBot && !b.isPet)
                    continue;
                for (SurvivalAbility const& a : b.survival)
                {
                    RescueValue::Option const o = SurvivalValue::Save(world, m, danger, b, a);
                    if (o.Net() <= 0.0f)
                        continue;
                    DutyCandidate c;
                    c.bot = b.guid;
                    c.spellId = a.spellId;
                    // Consumables (healthstone, healing potion) after every spell that pays
                    // (user 2026-11-15): only when no bot with one is free.
                    c.rank = a.itemId ? o.Net() - CONSUMABLE_RANK_OFFSET : o.Net();
                    c.value = o.Net();
                    c.valued = true;
                    need.candidates.push_back(c);
                    consumable.push_back(a.itemId != 0);
                    need.points = std::max(need.points, o.Net());
                }
            }
            // The same rule for the allocator's totals: a consumable is worth less than every
            // spell candidate, so it only wins when the spell bots are worth more elsewhere.
            float lowestSpell = std::numeric_limits<float>::max();
            for (size_t i = 0; i < need.candidates.size(); ++i)
                if (!consumable[i])
                    lowestSpell = std::min(lowestSpell, need.candidates[i].value);
            for (size_t i = 0; i < need.candidates.size(); ++i)
                if (consumable[i])
                    need.candidates[i].value = std::min(need.candidates[i].value, lowestSpell - CONSUMABLE_VALUE_GAP);
            if (!need.candidates.empty())
                out.push_back(std::move(need));
        }
    }

    void RescueAssigner::Apply(WorldModel const& world, std::vector<DutyResult> const& results, TeamPlan& plan) const
    {
        std::vector<RescueDuty> rescues;
        for (DutyResult const& r : results)
        {
            if (r.kind == DutyKind::Save && !r.primary.IsEmpty())
            {
                RescueDuty d;
                d.victim = r.target;
                d.bot = r.primary;
                d.spellId = r.primarySpellId;
                d.points = r.points;
                d.urgent = true;
                d.way = RescueWay::Save;
                rescues.push_back(d);
                continue;
            }
            if (r.kind != DutyKind::Rescue || r.primary.IsEmpty())
                continue;
            EnemyFacts const* e = world.FindEnemy(r.target);
            if (!e)
                continue;
            RescueDuty d;
            d.enemy = r.target;
            d.victim = e->victim;
            d.bot = r.primary;
            d.spellId = r.primarySpellId;
            d.points = r.points;
            d.urgent = (r.detail & DETAIL_URGENT) != 0;
            if (!d.spellId)
                d.way = RescueWay::Threat;
            else if (d.bot == d.victim)
                d.way = RescueWay::ThreatDrop;
            else
            {
                d.way = RescueWay::Taunt;
                if (PartyMemberFacts const* tank = world.FindMember(d.bot))
                {
                    float const wait = TauntWaitSec(world, plan, *e, *tank, d.spellId, d.urgent, &d.waitGain);
                    if (wait > 0.0f)
                        d.waitUntilMs = world.refreshedAtMs + uint32(wait * 1000.0f);
                }
            }
            rescues.push_back(d);
        }
        plan.rescues = std::move(rescues);
    }

    void RescueAssigner::Clear(TeamPlan& plan) const
    {
        plan.rescues.clear();
    }
}
