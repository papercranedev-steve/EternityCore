#include "Commander/Perception/Values/CurseValue.h"
#include "Commander/Perception/Values/MitigationValue.h"
#include "Commander/Perception/Values/ValueModel.h"
#include <algorithm>

namespace Commander
{
namespace CurseValue
{
    namespace
    {
        // Share of its damage `member` gains from the curse.
        float Gain(EnemyFacts const& enemy, PartyMemberFacts const& member, CurseEffect const& curse)
        {
            if (curse.kind != CurseKind::Recklessness)
                return curse.damagePct;
            if (enemy.armor == 0 || curse.armorCut <= 0.0f)
                return 0.0f;
            uint32 const after = uint32(std::max(0.0f, float(enemy.armor) - curse.armorCut));
            float const before = 1.0f - Value::ArmorReduction(enemy.armor, member.level);
            if (before <= 0.0f)
                return 0.0f;
            return (1.0f - Value::ArmorReduction(after, member.level)) / before - 1.0f;
        }
    }

    float HorizonSec(WorldModel const& world, EnemyFacts const& enemy, CurseEffect const& curse)
    {
        return std::max(0.0f, std::min(curse.durationSec, Value::EnemyTimeToDie(world, enemy)));
    }

    float Benefit(WorldModel const& world, EnemyFacts const& enemy, CurseEffect const& curse)
    {
        if (curse.kind == CurseKind::None || !enemy.acting)
            return 0.0f;
        float const horizon = HorizonSec(world, enemy, curse);
        if (horizon <= 0.0f)
            return 0.0f;
        float perSec = 0.0f;
        for (PartyMemberFacts const& m : world.party)
        {
            if (m.isPet || !m.CanAct() || WarlockCurses::Favoured(m.classId, m.role) != curse.kind)
                continue;
            perSec += Value::ContributionPerSec(world, m) * Gain(enemy, m, curse);
        }
        return perSec * horizon;
    }

    float Cost(WorldModel const& world, PartyMemberFacts const& caster, EnemyFacts const& enemy,
               CurseEffect const& curse)
    {
        float cost = Value::LostAction(world, caster, Value::GCD_SEC);
        if (curse.apAdded > 0.0f)
            if (PartyMemberFacts const* victim = world.FindMember(enemy.victim))
                cost += Value::Points(world, MitigationValue::SavedDps(enemy, *victim, curse.apAdded) *
                                             HorizonSec(world, enemy, curse));
        return cost;
    }
}
}
