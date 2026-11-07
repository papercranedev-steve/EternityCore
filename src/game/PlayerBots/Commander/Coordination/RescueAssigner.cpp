#include "Commander/Coordination/RescueAssigner.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/LooseEnemy.h"
#include "Commander/Perception/PackResolver.h"
#include "Commander/Perception/RescueValue.h"
#include "Commander/Perception/RouteRisk.h"
#include "Commander/Perception/Tanks.h"
#include "Commander/Perception/WorldModel.h"
#include <algorithm>

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

    void RescueAssigner::Offer(WorldModel const& world, TeamPlan const& plan, std::vector<DutyNeed>& out) const
    {
        if (!world.HasPartyEnemy())
            return;

        std::vector<PartyMemberFacts const*> tanks;
        for (PartyMemberFacts const& m : world.party)
            if (m.isBot && !m.isPet && m.CanAct() && Tanks::IsTank(m))
                tanks.push_back(&m);
        float const enemyDps = RescueValue::EnemyDps(world);

        for (EnemyFacts const& e : world.enemies)
        {
            PartyMemberFacts const* victim = RescueValue::LooseVictim(world, e);
            if (!victim || plan.patrolMove.Avoids(e.guid) || plan.orders.LeavesAlone(e.guid))
                continue;

            DutyNeed need;
            need.kind = DutyKind::Rescue;
            need.target = e.guid;
            need.now = true;
            need.exclusive = DutyExclusive::EnemyHandled;
            need.reason = "loose enemy";
            auto const add = [&](ObjectGuid bot, uint32 spellId, RescueValue::Option const& o)
            {
                if (o.Net() <= 0.0f)
                    return;
                DutyCandidate c;
                c.bot = bot;
                c.spellId = spellId;
                c.rank = o.Net() + (WasGiven(plan, bot, e.guid, spellId) ? STICKY_POINTS : 0.0f);
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
                bool const walkBlocked = tank->victim != e.guid && distance > MELEE_REACH + e.boundingRadius &&
                                         RouteRisk::Blocked(world, *tank, e);
                if (!walkBlocked)
                    add(tank->guid, 0, RescueValue::Threat(world, e, *victim, *tank, tankRate, distance));
                for (TauntAbility const& t : tank->taunts)
                {
                    if (walkBlocked && distance - e.boundingRadius > t.maxRange)
                        continue;
                    add(tank->guid, t.spellId, RescueValue::Taunt(world, e, *victim, *tank, t, tankRate, distance, enemyDps));
                }
            }

            if (victim->isBot)
                add(victim->guid, victim->threatDrop.spellId, RescueValue::ThreatDrop(world, e, *victim));

            if (!need.candidates.empty())
                out.push_back(std::move(need));
        }
    }

    void RescueAssigner::Apply(WorldModel const& world, std::vector<DutyResult> const& results, TeamPlan& plan) const
    {
        std::vector<RescueDuty> rescues;
        for (DutyResult const& r : results)
        {
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
                d.way = RescueWay::Taunt;
            rescues.push_back(d);
        }
        plan.rescues = std::move(rescues);
    }

    void RescueAssigner::Clear(TeamPlan& plan) const
    {
        plan.rescues.clear();
    }
}
