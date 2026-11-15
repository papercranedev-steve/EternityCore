#include "Commander/Coordination/Duties/FleeStopAssigner.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/Values/FleeStopValue.h"
#include "Commander/Perception/Spatial/PackResolver.h"
#include "Commander/Perception/WorldModel.h"
#include <algorithm>

namespace Commander
{
    namespace
    {
        FleeStopAbility const* FindAbility(WorldModel const& world, ObjectGuid bot, uint32 spellId)
        {
            if (PartyMemberFacts const* m = world.FindMember(bot))
                for (FleeStopAbility const& a : m->fleeStops)
                    if (a.spellId == spellId)
                        return &a;
            return nullptr;
        }

        bool Due(EnemyFacts const& e)
        {
            return e.flee.fleeing || e.healthPct <= e.flee.atPct + FleeStopAssigner::TRIGGER_MARGIN_PCT;
        }
    }

    void FleeStopAssigner::Offer(WorldModel const& world, TeamPlan const& plan, std::vector<DutyNeed>& out) const
    {
        if (!world.HasPartyEnemy())
            return;
        for (EnemyFacts const& e : world.enemies)
        {
            FleeFacts const& f = e.flee;
            if (!f.predicted || f.spent || f.prevented || f.focusedPoints <= 0.0f || !e.acting || !e.health)
                continue;
            if (!plan.MayEngage(ObjectGuid(), e.guid))
                continue;

            DutyNeed need;
            need.kind = DutyKind::FleeStop;
            need.target = e.guid;
            need.now = Due(e);
            need.reason = f.fleeing ? "running away" : "about to run away";
            ObjectGuid previous;
            for (FleeStopDuty const& d : plan.fleeStops)
                if (d.enemy == e.guid)
                    previous = d.bot;

            for (PartyMemberFacts const& m : world.party)
            {
                if (!m.isBot || m.isPet || !m.CanAct())
                    continue;
                float const distance = PackResolver::Distance(m.pos, e.pos) - e.boundingRadius;
                for (FleeStopAbility const& a : m.fleeStops)
                {
                    if (!a.ready)
                        continue;
                    // A slow or a catch-up is for a runner: before that the focused kill may do.
                    if (a.holdSec <= 0.0f && !f.fleeing)
                        continue;
                    if (a.speedPct > 0.0f)
                    {
                        // Catch-up: the bot chases it already (its victim); the value counts the gap.
                        if (m.victim != e.guid)
                            continue;
                    }
                    else
                    {
                        float const range = a.radius > 0.0f ? a.radius : a.maxRange;
                        if (distance > range + RESERVE_SLACK_YD)
                            continue;
                    }
                    if (a.needsCombo && !(m.comboTarget == e.guid && m.comboPoints > 0) && m.victim != e.guid)
                        continue;
                    FleeStopValue::Worth const w = FleeStopValue::Of(world, m, a, e);
                    if (w.Net() <= 0.0f)
                        continue;
                    DutyCandidate c;
                    c.bot = m.guid;
                    c.spellId = a.spellId;
                    c.rank = w.Net() + (m.guid == previous ? STICKY_POINTS : 0.0f);
                    need.candidates.push_back(c);
                    need.points = std::max(need.points, w.Net());
                }
            }
            if (!need.candidates.empty())
                out.push_back(std::move(need));
        }
    }

    void FleeStopAssigner::Apply(WorldModel const& world, std::vector<DutyResult> const& results, TeamPlan& plan) const
    {
        std::vector<FleeStopDuty> duties;
        for (DutyResult const& r : results)
        {
            if (r.kind != DutyKind::FleeStop || r.primary.IsEmpty())
                continue;
            EnemyFacts const* e = world.FindEnemy(r.target);
            FleeStopAbility const* a = FindAbility(world, r.primary, r.primarySpellId);
            if (!e || !a)
                continue;
            FleeStopDuty d;
            d.enemy = r.target;
            d.bot = r.primary;
            d.spellId = r.primarySpellId;
            d.points = r.points;
            d.hold = a->holdSec > 0.0f;
            d.holdSec = a->holdSec;
            d.catchUp = a->speedPct > 0.0f;
            d.needsCombo = a->needsCombo;
            d.triggerPct = e->flee.atPct + TRIGGER_MARGIN_PCT;
            duties.push_back(d);
        }
        plan.fleeStops = std::move(duties);
    }

    void FleeStopAssigner::Clear(TeamPlan& plan) const
    {
        plan.fleeStops.clear();
    }
}
