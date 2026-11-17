#include "Commander/Coordination/Duties/BoostAssigner.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/WorldModel.h"
#include "Commander/Perception/Values/BoostValue.h"
#include <algorithm>

namespace Commander
{
    void BoostAssigner::Clear(TeamPlan& plan) const
    {
        plan.boosts.clear();
    }

    void BoostAssigner::Offer(WorldModel const& world, TeamPlan const& plan, std::vector<DutyNeed>& out) const
    {
        if (!world.HasPartyEnemy())
            return;

        for (PartyMemberFacts const& target : world.party)
        {
            if (target.isPet || !target.alive)
                continue;
            DutyNeed need;
            need.kind = DutyKind::Boost;
            need.target = target.guid;
            for (PartyMemberFacts const& bot : world.party)
            {
                if (!bot.isBot || bot.isPet || !bot.CanAct())
                    continue;
                for (AllyBoostAbility const& a : bot.allyBoosts)
                {
                    RescueValue::Option const o = BoostValue::Of(world, bot, a, target);
                    float const net = o.Net();
                    if (o.benefit <= 0.0f)
                        continue;
                    DutyCandidate c;
                    c.bot = bot.guid;
                    c.spellId = a.spellId;
                    c.rank = net;
                    for (BoostDuty const& d : plan.boosts)
                        if (d.bot == bot.guid && d.target == target.guid && d.spellId == a.spellId)
                            c.rank += STICKY_POINTS;
                    c.value = c.rank;
                    c.valued = true;
                    if (net <= 0.0f)
                        need.rejected.push_back(c);
                    else
                    {
                        need.candidates.push_back(c);
                        need.points = std::max(need.points, net);
                    }
                }
            }
            if (need.candidates.empty() && need.rejected.empty())
                continue;
            std::stable_sort(need.candidates.begin(), need.candidates.end(),
                [](DutyCandidate const& a, DutyCandidate const& b) { return a.rank > b.rank; });
            out.push_back(std::move(need));
        }
    }

    void BoostAssigner::Apply(WorldModel const& /*world*/, std::vector<DutyResult> const& results, TeamPlan& plan) const
    {
        std::vector<BoostDuty> duties;
        for (DutyResult const& r : results)
        {
            if (r.kind != DutyKind::Boost || r.primary.IsEmpty())
                continue;
            BoostDuty d;
            d.bot = r.primary;
            d.target = r.target;
            d.spellId = r.primarySpellId;
            d.points = r.points;
            duties.push_back(d);
        }
        plan.boosts = std::move(duties);
    }
}
