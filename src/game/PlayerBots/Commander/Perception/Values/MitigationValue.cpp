#include "Commander/Perception/Values/MitigationValue.h"
#include "Commander/Perception/Values/ValueModel.h"
#include "Commander/Perception/Spatial/PackResolver.h"
#include <algorithm>

namespace Commander
{
namespace MitigationValue
{
    namespace
    {
        // Creature::UpdateDamagePhysical's weapon damage factor.
        float Factor(EnemyFacts const& e, float ap)
        {
            return 0.7f + 0.3f * std::max(0.0f, ap) / e.apDb;
        }
    }

    float SavedDps(EnemyFacts const& e, PartyMemberFacts const& victim, float apOff)
    {
        if (e.apDb <= 0.0f || apOff <= 0.0f || !e.acting || e.crowdControlled || !e.meleeEnabled)
            return 0.0f;
        float const melee = std::max(0.0f, Value::HitDps(e, victim) - e.rangedDps);
        float const now = Factor(e, e.apNow);
        if (now <= 0.0f)
            return 0.0f;
        float const less = now - Factor(e, e.apNow - apOff);
        return melee * std::max(0.0f, less) / now;
    }

    float Benefit(WorldModel const& world, MitigationAbility const& a, Position3 const& caster)
    {
        float saved = 0.0f;     // health over the duration
        for (EnemyFacts const& e : world.enemies)
        {
            if (PackResolver::Distance(caster, e.pos) - e.boundingRadius > a.radius)
                continue;
            PartyMemberFacts const* victim = world.FindMember(e.victim);
            if (!victim || !victim->alive)
                continue;
            float const horizon = std::min(a.durationSec, Value::EnemyTimeToDie(world, e));
            if (horizon <= 0.0f)
                continue;
            // While the debuff already on it lasts: only what this one adds above it.
            float const overlap = std::min(horizon, e.apDebuffMs / 1000.0f);
            if (overlap > 0.0f && a.reduction > e.apDebuff)
                saved += SavedDps(e, *victim, a.reduction - e.apDebuff) * overlap;
            // After it runs out: its attack power comes back, this one takes `reduction` off it.
            // (The factor is linear in attack power: the same `reduction` saves the same damage
            // per second whatever the attack power it starts from, so SavedDps at apNow is exact.)
            saved += SavedDps(e, *victim, a.reduction) * (horizon - overlap);
        }
        return Value::Points(world, saved);
    }

    bool PullsIdle(WorldModel const& world, MitigationAbility const& a, Position3 const& caster)
    {
        float const reach = a.radius + IDLE_MARGIN_YD;
        for (PackFacts const& pack : world.packs)
            for (PackCreature const& c : pack.members)
                if (PackResolver::Distance(caster, c.pos) <= reach)
                    return true;
        for (PatrolFacts const& p : world.patrols)
            if (PackResolver::Distance(caster, p.pos) <= reach)
                return true;
        return false;
    }

    float Cost(WorldModel const& world, PartyMemberFacts const& caster, MitigationAbility const& a)
    {
        return Value::LostAction(world, caster, Value::GCD_SEC) * (1.0f + float(a.rageCost) / RAGE_PER_ACTION);
    }
}
}
