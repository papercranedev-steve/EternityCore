#include "Commander/Coordination/Duties/ResurrectAssigner.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/Spatial/PackResolver.h"
#include "Commander/Perception/Values/ResurrectValue.h"
#include "Commander/Perception/WorldModel.h"
#include <algorithm>

namespace Commander
{
    void ResurrectAssigner::Clear(TeamPlan& plan) const
    {
        plan.resurrections.clear();
    }

    void ResurrectAssigner::Offer(WorldModel const& world, TeamPlan const& plan, std::vector<DutyNeed>& out) const
    {
        if (!world.HasPartyEnemy())
            return;

        for (CorpseFacts const& dead : world.corpses)
        {
            if (dead.resRequested)
                continue;
            DutyNeed need;
            need.kind = DutyKind::Resurrect;
            need.target = dead.guid;
            need.now = true;
            need.reason = dead.role == ROLE_HEALER ? "dead healer" : "dead tank";
            for (PartyMemberFacts const& bot : world.party)
            {
                CombatResAbility const& a = bot.combatRes;
                if (!bot.isBot || bot.isPet || !bot.CanAct() || !a.spellId || !a.ready)
                    continue;
                if (PackResolver::Distance(bot.pos, dead.pos) > a.maxRange)
                    continue;
                RescueValue::Option const o = ResurrectValue::Of(world, bot, a, dead);
                if (o.benefit <= 0.0f)
                    continue;
                DutyCandidate c;
                c.bot = bot.guid;
                c.spellId = a.spellId;
                c.rank = o.Net();
                for (ResurrectDuty const& d : plan.resurrections)
                    if (d.bot == bot.guid && d.target == dead.guid)
                        c.rank += STICKY_POINTS;
                c.value = c.rank;
                c.valued = true;
                if (o.Net() <= 0.0f)
                    need.rejected.push_back(c);
                else
                {
                    need.candidates.push_back(c);
                    need.points = std::max(need.points, o.Net());
                }
            }
            if (need.candidates.empty() && need.rejected.empty())
                continue;
            std::stable_sort(need.candidates.begin(), need.candidates.end(),
                [](DutyCandidate const& a, DutyCandidate const& b) { return a.rank > b.rank; });
            out.push_back(std::move(need));
        }
    }

    void ResurrectAssigner::Apply(WorldModel const& /*world*/, std::vector<DutyResult> const& results, TeamPlan& plan) const
    {
        std::vector<ResurrectDuty> duties;
        for (DutyResult const& r : results)
        {
            if (r.kind != DutyKind::Resurrect || r.primary.IsEmpty())
                continue;
            ResurrectDuty d;
            d.bot = r.primary;
            d.target = r.target;
            d.spellId = r.primarySpellId;
            d.points = r.points;
            duties.push_back(d);
        }
        plan.resurrections = std::move(duties);
    }
}
