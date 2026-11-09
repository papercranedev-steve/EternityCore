#include "Commander/Encounters/EncounterHelpers.h"
#include "Commander/Coordination/Duties/FocusAssigner.h"
#include "Commander/Coordination/Duties/TargetAssigner.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/WorldModel.h"
#include "Commander/Perception/Values/ValueModel.h"
#include "Commander/Perception/Tanks.h"
#include "Commander/CommanderMgr.h"
#include "PartyBotAI.h"
#include <algorithm>

namespace Commander
{
namespace EncounterHelpers
{
    EnemyFacts const* FindEnemyByEntry(WorldModel const& world, uint32 entry)
    {
        for (EnemyFacts const& e : world.enemies)
            if (e.entry == entry)
                return &e;
        return nullptr;
    }

    bool HasEnemyEntry(WorldModel const& world, uint32 entry)
    {
        return FindEnemyByEntry(world, entry) != nullptr;
    }

    PartyMemberFacts const* FindTank(WorldModel const& world)
    {
        return Tanks::Main(world);
    }

    PartyMemberFacts const* FindHolder(WorldModel const& world, EnemyFacts const* enemy)
    {
        if (enemy && !enemy->victim.IsEmpty())
            if (PartyMemberFacts const* v = world.FindMember(enemy->victim))
                if (!v->isPet && v->alive)
                    return v;
        return FindTank(world);
    }

    PartyMemberFacts const* FindLowestManaHealer(WorldModel const& world)
    {
        PartyMemberFacts const* best = nullptr;
        for (PartyMemberFacts const& m : world.party)
        {
            if (!m.isBot || m.isPet || m.role != ROLE_HEALER || !m.CanAct() || m.manaPct < 0.0f)
                continue;
            if (!best || m.manaPct < best->manaPct)
                best = &m;
        }
        return best;
    }

    void SetFocus(WorldModel const& world, TeamPlan& plan, EnemyFacts const& enemy, char const* reason)
    {
        FocusScore const s = FocusAssigner::Evaluate(world, enemy);
        if (plan.focus.enemy != enemy.guid)
            plan.focus.sinceMs = world.refreshedAtMs;
        plan.focus.enemy = enemy.guid;
        plan.focus.score = s.score;
        plan.focus.harmRate = s.harmRate;
        plan.focus.killSec = s.killSec;
        plan.focus.reason = reason;
        RedistributeTargets(world, plan);
    }

    void ClearFocus(WorldModel const& world, TeamPlan& plan)
    {
        plan.focus = FocusTarget();
        RedistributeTargets(world, plan);
    }

    void RedistributeTargets(WorldModel const& world, TeamPlan& plan)
    {
        if (sCommanderMgr.IsModuleEnabled("target"))
            TargetAssigner::Distribute(world, plan);
    }

    float HealDelayCost(WorldModel const& world, PartyMemberFacts const& target)
    {
        float const delaySec = 2.0f * sCommanderMgr.GetBotUpdateInterval() / 1000.0f;
        float const missing = float(target.maxHealth > target.health ? target.maxHealth - target.health : 0);
        float const incoming = Value::IncomingOn(world, target.guid);
        float cost = Value::Points(world, std::min(missing, incoming * delaySec));
        if (incoming * delaySec >= float(target.health))
            cost += Value::DeathPenalty(target);
        return cost;
    }

    Proposal MakeReplace(char const* source, ProposalTier tier, BotActionType type,
                         ObjectGuid target, SpellEntry const* spell)
    {
        Proposal p;
        p.tier = tier;
        p.source = source;
        p.action.type = type;
        p.action.target = target;
        p.action.spell = spell;
        return p;
    }

    Proposal MakeHealVeto(char const* source, ObjectGuid target)
    {
        Proposal p;
        p.kind = ProposalKind::Veto;
        p.tier = ProposalTier::Assigned;
        p.source = source;
        p.veto.type = VetoType::Heal;
        p.veto.target = target;
        return p;
    }
}
}
