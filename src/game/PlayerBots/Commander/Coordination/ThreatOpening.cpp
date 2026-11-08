#include "Commander/Coordination/ThreatOpening.h"
#include "Commander/Coordination/PullOpening.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/PackResolver.h"
#include "Commander/Perception/SurvivalValue.h"
#include "Commander/Perception/Tanks.h"
#include "Commander/Perception/ThreatGap.h"
#include "Commander/Perception/WorldModel.h"
#include "Timer.h"
#include <algorithm>
#include <cstdio>

namespace Commander
{
    namespace
    {
        bool Contains(std::vector<ObjectGuid> const& list, ObjectGuid g)
        {
            return std::find(list.begin(), list.end(), g) != list.end();
        }

        // An enemy the opening is about: fighting the party, able to act, next to the tank.
        bool NextToTank(WorldModel const& world, EnemyFacts const& e, PartyMemberFacts const& tank)
        {
            if (!e.acting || !e.health || e.crowdControlled || e.onAllyOnly || !world.FindMember(e.victim))
                return false;
            return PackResolver::Distance(e.pos, tank.pos) - e.boundingRadius <= ThreatOpeningAssigner::REACH_YD;
        }

        // Pulled enemies still walking to the tank (see the header).
        bool PullComing(WorldModel const& world, PullPlan const& pull, PartyMemberFacts const& tank, uint32 now)
        {
            if (pull.phase != PullPhase::Settle || pull.awaitArrival || pull.tank != tank.guid ||
                WorldTimer::getMSTimeDiff(pull.phaseSinceMs, now) > PullOpening::HOLD_MAX_MS)
                return false;
            for (EnemyFacts const& e : world.enemies)
                if (Contains(pull.expected, e.guid) && e.acting && !e.crowdControlled && e.meleeEnabled &&
                    e.victim == tank.guid && !NextToTank(world, e, tank))
                    return true;
            return false;
        }

        float Total(std::vector<ThreatGapEntry> const& gaps)
        {
            float t = 0.0f;
            for (ThreatGapEntry const& g : gaps)
                t += g.gap;
            return t;
        }

        std::string Text(char const* fmt, float a, float b = 0.0f, float c = 0.0f)
        {
            char buf[200];
            snprintf(buf, sizeof(buf), fmt, a, b, c);
            return buf;
        }

        void End(ThreatOpeningPlan& t, std::string note, std::vector<std::string>& notes)
        {
            for (ThreatGapEntry const& g : t.gaps)
                if (!Contains(t.settled, g.enemy))
                    t.settled.push_back(g.enemy);
            t.active = false;
            t.gaps.clear();
            t.freed.clear();
            t.healAllowed = false;
            notes.push_back(std::move(note));
        }
    }

    void ThreatOpeningAssigner::Assign(WorldModel const& world, TeamPlan& plan) const
    {
        ThreatOpeningPlan& t = plan.threat;
        if (!world.HasPartyEnemy())
        {
            t = ThreatOpeningPlan();     // new fight, every enemy needs its opening
            return;
        }
        uint32 const now = world.refreshedAtMs;
        float const elapsed = t.active ? WorldTimer::getMSTimeDiff(t.sinceMs, now) / 1000.0f : 0.0f;

        // Settled enemies that are gone.
        t.settled.erase(std::remove_if(t.settled.begin(), t.settled.end(),
            [&world](ObjectGuid g) { return !world.FindEnemy(g); }), t.settled.end());

        PartyMemberFacts const* tank = Tanks::Main(world, true);
        if (!tank || !tank->CanAct() || plan.retreat.active)
        {
            if (t.active)
                End(t, Text("Threat opening ended after %.0f s: the tank cannot build threat now.", elapsed), plan.notes);
            return;
        }

        std::vector<ThreatGapEntry> gaps;
        for (EnemyFacts const& e : world.enemies)
        {
            if (t.Settled(e.guid) || !NextToTank(world, e, *tank))
                continue;
            float const gap = ThreatGap::Of(world, e, tank->guid);
            if (gap <= 0.0f)
                t.settled.push_back(e.guid);
            else
                gaps.push_back({ e.guid, gap });
        }
        bool const coming = PullComing(world, plan.pull, *tank, now);

        if (gaps.empty() && !coming)
        {
            if (t.active)
                End(t, Text("Threat opening done in %.0f s: the tank holds every enemy next to it.", elapsed), plan.notes);
            return;
        }

        float const total = Total(gaps);
        bool newEnemy = !t.active || t.tank != tank->guid;
        for (ThreatGapEntry const& g : gaps)
            newEnemy = newEnemy || !std::any_of(t.gaps.begin(), t.gaps.end(),
                [&g](ThreatGapEntry const& o) { return o.enemy == g.enemy; });
        if (!t.active)
        {
            t.active = true;
            t.sinceMs = now;
            plan.notes.push_back(gaps.empty()
                ? std::string("Threat opening: the pack is still coming to the tank; the others wait.")
                : Text("Threat opening: the tank builds threat on %.0f enemies (%.0f short in total); the others wait.",
                       float(gaps.size()), total));
        }
        t.tank = tank->guid;
        // Progress: the total gap shrank (a new enemy next to the tank starts the count again).
        // While the pack is still coming and nothing is next to the tank, there is nothing to stall.
        if (newEnemy || gaps.empty() || total < t.bestGap - PROGRESS_MIN)
        {
            t.bestGap = total;
            t.progressMs = now;
        }
        t.gaps = std::move(gaps);
        t.waitPerSec = ThreatGap::WaitCostPerSec(world, tank->guid);
        t.fillRate = ThreatGap::FillRate(world, *tank);

        if (!t.gaps.empty() && WorldTimer::getMSTimeDiff(t.progressMs, now) > STALL_MS)
        {
            End(t, Text("Threat opening ended after %.0f s: no progress for %.0f s (%.0f threat still short); fighting.",
                        elapsed, STALL_MS / 1000.0f, Total(t.gaps)), plan.notes);
            return;
        }

        // Who may act anyway: members about to die (healers heal them, they save themselves).
        t.freed.clear();
        t.healAllowed = false;
        for (PartyMemberFacts const& m : world.party)
        {
            if (m.isPet || !m.alive)
                continue;
            if (m.healthPct < HEAL_BELOW_PCT || SurvivalValue::Of(world, m).InDanger())
            {
                t.healAllowed = true;
                t.freed.push_back(m.guid);
            }
        }
    }

    void ThreatOpeningAssigner::Clear(TeamPlan& plan) const
    {
        plan.threat = ThreatOpeningPlan();
    }
}
