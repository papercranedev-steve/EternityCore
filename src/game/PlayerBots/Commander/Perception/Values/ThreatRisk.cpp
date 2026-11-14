#include "Commander/Perception/Values/ThreatRisk.h"
#include "Commander/Perception/Values/LooseEnemy.h"
#include "Commander/Perception/Values/ValueModel.h"
#include "Commander/Perception/Profiles/Tags.h"
#include <algorithm>

namespace Commander
{
namespace ThreatRisk
{
    bool Counts(EnemyFacts const& e, ObjectGuid tank)
    {
        if (e.victim != tank || !e.acting || !e.health || e.crowdControlled)
            return false;
        return !(Tags::RolesOf(e.entry) & TAG_SELF_DESTRUCT) && Tags::SourceOf(e) != Source::Trap;
    }

    float Of(WorldModel const& world, EnemyFacts const& e, ObjectGuid tank,
             std::vector<PlannedThreat> const& planned, float horizonSec, float addTank)
    {
        PartyMemberFacts const* holder = world.FindMember(tank);
        if (!holder)
            return 0.0f;
        float const ttd = Value::EnemyTimeToDie(world, e);
        float const T = std::min(horizonSec, ttd);
        if (T <= 0.0f)
            return 0.0f;
        float risk = 0.0f;
        for (ThreatRunway::Rival const& r : ThreatRunway::Rivals(world, e, tank, planned, addTank))
        {
            PartyMemberFacts const* m = world.FindMember(r.member);
            if (!m || m->isPet || !m->alive)
                continue;
            float const p = std::min(1.0f, 1.0f - std::max(0.0f, r.sec) / T);
            if (p <= 0.0f)
                continue;
            // Only while it lives once m has it: an enemy that dies in a second (an Eye of
            // Naxxramas, 270 hp) costs that second (user 2026-11-14).
            float const lossSec = std::min(RECLAIM_SEC, ttd - std::max(0.0f, r.sec));
            if (lossSec <= 0.0f)
                continue;
            risk = std::max(risk, p * LooseEnemy::Loss(world, e, *m, *holder, lossSec));
        }
        return risk;
    }
}
}
