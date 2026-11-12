#include "Commander/Perception/Forecast/ThreatRunway.h"
#include "Commander/Perception/Forecast/ThreatView.h"
#include <algorithm>
#include <limits>

namespace Commander
{
namespace ThreatRunway
{
    namespace
    {
        float const NEVER = std::numeric_limits<float>::infinity();

        // Enemies the heal's threat is split over: those with the tank on their list.
        uint32 SplitCount(WorldModel const& world, ObjectGuid tank)
        {
            uint32 n = 0;
            for (EnemyFacts const& e : world.enemies)
                if (ThreatEntry const* t = ThreatView::FindEntry(e, tank))
                    if (t->onList)
                        ++n;
            return std::max<uint32>(1, n);
        }

        float HealBurst(PartyMemberFacts const& m, uint32 split)
        {
            return m.isBot && !m.isPet && m.CanAct() && m.role == ROLE_HEALER ? m.healThreat / float(split) : 0.0f;
        }

        float PlannedOf(std::vector<PlannedThreat> const& planned, ObjectGuid member)
        {
            float rate = 0.0f;
            for (PlannedThreat const& p : planned)
                if (p.member == member)
                    rate = std::max(rate, p.perSec);
            return rate;
        }

        // Seconds until a rival with `rivalThreat` gaining `gain` per second takes it.
        float SecOf(float own, float rivalThreat, float gain, float factor)
        {
            float const lead = own * factor - rivalThreat;
            return lead <= 0.0f ? 0.0f : gain > 0.0f ? lead / gain : NEVER;
        }

        // One rival of the tank: its runway and the threat the tank needs for `horizonSec`.
        void AddRival(float own, float rivalThreat, float rivalRate, float tankRate, float factor,
                      float horizonSec, ObjectGuid guid, Result& out)
        {
            float const gain = rivalRate - tankRate * factor;
            float const needed = (rivalThreat + std::max(0.0f, gain) * horizonSec) / factor;
            out.shortfall = std::max(out.shortfall, needed - own);

            float const sec = SecOf(own, rivalThreat, gain, factor);
            if (sec < out.sec)
            {
                out.sec = sec;
                out.taker = guid;
            }
        }
    }

    float HolderRate(WorldModel const& /*world*/, EnemyFacts const& e, PartyMemberFacts const& holder)
    {
        if (holder.victim != e.guid)
            return 0.0f;
        ThreatEntry const* t = ThreatView::FindEntry(e, holder.guid);
        return t ? t->threatPerSec * HOLDER_RATE_SHARE : 0.0f;
    }

    namespace
    {
        // Every rival of the tank on `e`: fn(own, rival's threat incl. its heal burst, rival's
        // rate, tank rate, aggro factor, guid). Returns the tank's threat (with `addTank`).
        template <class Fn>
        float ForEachRival(WorldModel const& world, EnemyFacts const& e, ObjectGuid tank,
                           std::vector<PlannedThreat> const& planned, float addTank, Fn&& fn)
        {
            uint32 const split = SplitCount(world, tank);
            PartyMemberFacts const* tankFacts = world.FindMember(tank);
            float const tankRate = tankFacts ? HolderRate(world, e, *tankFacts) : 0.0f;

            float own = addTank;
            if (ThreatEntry const* t = ThreatView::FindEntry(e, tank))
                own += t->threat;

            for (ThreatEntry const& t : e.threat)
            {
                if (t.guid == tank || !t.partySide)
                    continue;
                float threat = t.onList ? t.threat : 0.0f;
                PartyMemberFacts const* m = world.FindMember(t.guid);
                if (m && !e.ignoresAssistThreat)
                    threat += HealBurst(*m, split);
                float const rate = std::max(t.threatPerSec, PlannedOf(planned, t.guid));
                float const factor = t.inMeleeReach ? ThreatView::MELEE_AGGRO_FACTOR : ThreatView::RANGED_AGGRO_FACTOR;
                fn(own, threat, rate, tankRate, factor, t.guid);
            }
            // Members with no entry at all: a healer bot's heal, a planned area spell.
            for (PartyMemberFacts const& m : world.party)
            {
                if (m.guid == tank || ThreatView::FindEntry(e, m.guid))
                    continue;
                float const burst = e.ignoresAssistThreat ? 0.0f : HealBurst(m, split);
                float const rate = PlannedOf(planned, m.guid);
                if (burst > 0.0f || rate > 0.0f)
                    fn(own, burst, rate, tankRate, ThreatView::RANGED_AGGRO_FACTOR, m.guid);
            }
            return own;
        }
    }

    Result Of(WorldModel const& world, EnemyFacts const& e, ObjectGuid tank,
              std::vector<PlannedThreat> const& planned, float horizonSec, float addTank)
    {
        Result out;
        out.sec = NEVER;
        float const own = ForEachRival(world, e, tank, planned, addTank,
            [&](float own, float threat, float rate, float tankRate, float factor, ObjectGuid guid)
            {
                AddRival(own, threat, rate, tankRate, factor, horizonSec, guid, out);
            });

        if (own <= 0.0f)
        {
            out.sec = 0.0f;
            out.shortfall = std::max(out.shortfall, 1.0f);
        }
        out.shortfall = std::max(0.0f, out.shortfall);
        return out;
    }

    std::vector<Rival> Rivals(WorldModel const& world, EnemyFacts const& e, ObjectGuid tank,
                              std::vector<PlannedThreat> const& planned, float addTank)
    {
        std::vector<Rival> out;
        ForEachRival(world, e, tank, planned, addTank,
            [&](float own, float threat, float rate, float tankRate, float factor, ObjectGuid guid)
            {
                float const sec = own <= 0.0f ? 0.0f : SecOf(own, threat, rate - tankRate * factor, factor);
                if (sec < NEVER)
                    out.push_back({ guid, sec });
            });
        return out;
    }
}
}
