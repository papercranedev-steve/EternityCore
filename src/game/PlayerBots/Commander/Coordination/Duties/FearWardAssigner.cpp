#include "Commander/Coordination/Duties/FearWardAssigner.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/Forecast/IncomingTimeline.h"
#include "Commander/Perception/Tanks.h"
#include "Commander/Perception/Values/ValueModel.h"
#include "Commander/Perception/WorldModel.h"
#include <algorithm>

namespace Commander
{
    namespace
    {
        struct Candidate
        {
            PartyMemberFacts const* member = nullptr;
            float points = 0.0f;
            float atSec = 0.0f;
            uint32 fearSpellId = 0;
        };

        // The sure fears coming on `m` (header).
        Candidate Of(WorldModel const& world, PartyMemberFacts const& m)
        {
            Candidate c;
            c.member = &m;
            for (IncomingTimeline::Lockout const& l : IncomingTimeline::Of(world, m).lockouts)
            {
                if (!l.Fear() || l.startSec < FearWardAssigner::WARD_LEAD_SEC)
                    continue;
                if (c.points <= 0.0f)
                {
                    c.atSec = l.startSec;
                    c.fearSpellId = l.spellId;
                }
                c.points += Value::LostAction(world, m, l.endSec - l.startSec);
            }
            return c;
        }

        bool Wardable(PartyMemberFacts const& m)
        {
            return !m.isPet && m.alive && !m.charmed && !(m.immuneMechanics & (1u << MECHANIC_FEAR));
        }
    }

    void FearWardAssigner::Assign(WorldModel const& world, TeamPlan& plan) const
    {
        FearWardPlan out;
        std::vector<PartyMemberFacts const*> priests;
        for (PartyMemberFacts const& m : world.party)
            if (m.isBot && m.alive && m.fearWardSpellId)
                priests.push_back(&m);
        if (priests.empty())
        {
            plan.fearWards = std::move(out);
            return;
        }
        std::stable_sort(priests.begin(), priests.end(), [](PartyMemberFacts const* a, PartyMemberFacts const* b)
            { return a->guid.GetRawValue() < b->guid.GetRawValue(); });

        std::vector<Candidate> candidates;
        if (world.HasPartyEnemy())
            for (PartyMemberFacts const& m : world.party)
                if (Wardable(m))
                {
                    Candidate c = Of(world, m);
                    if (c.points > 0.0f)
                        candidates.push_back(c);
                }
        std::stable_sort(candidates.begin(), candidates.end(), [](Candidate const& a, Candidate const& b)
            { return a.points > b.points; });

        PartyMemberFacts const* tank = Tanks::Main(world);
        if (!tank)
            tank = Tanks::HitTaker(world);

        size_t next = 0;
        bool tankTaken = false;
        for (PartyMemberFacts const* p : priests)
        {
            FearWardDuty duty;
            duty.bot = p->guid;
            duty.spellId = p->fearWardSpellId;
            if (next < candidates.size())
            {
                Candidate const& c = candidates[next++];
                duty.target = c.member->guid;
                duty.points = c.points;
                duty.atSec = c.atSec;
                duty.fearSpellId = c.fearSpellId;
            }
            else if (tank && !tankTaken && Wardable(*tank))
            {
                // Kept up on the tank (header).
                duty.target = tank->guid;
                tankTaken = true;
            }
            else
                continue;
            if (duty.target == (tank ? tank->guid : ObjectGuid()))
                tankTaken = true;
            out.duties.push_back(duty);
        }
        plan.fearWards = std::move(out);
    }

    void FearWardAssigner::Clear(TeamPlan& plan) const
    {
        plan.fearWards = FearWardPlan();
    }
}
