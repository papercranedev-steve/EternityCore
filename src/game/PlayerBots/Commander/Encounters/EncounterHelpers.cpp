#include "Commander/Encounters/EncounterHelpers.h"
#include "Commander/Coordination/Duties/FocusAssigner.h"
#include "Commander/Coordination/Duties/TargetAssigner.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/WorldModel.h"
#include "Commander/Perception/Values/ValueModel.h"
#include "Commander/Perception/Tanks.h"
#include "Commander/Perception/Spatial/PackResolver.h"
#include "Commander/CommanderMgr.h"
#include "TeamBotAI.h"
#include "Timer.h"
#include <algorithm>

namespace Commander
{
namespace EncounterHelpers
{
    VoiceEvent Line(char const* key, VoiceTier tier, ObjectGuid speaker)
    {
        VoiceEvent e;
        e.key = key;
        e.tier = tier;
        e.speaker = speaker;
        e.onceMs = 30 * IN_MILLISECONDS;
        return e;
    }

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

    void AddKillNeed(WorldModel const& world, EnemyFacts const& e, float points, char const* reason,
                     Position3 const& tieTo, ObjectGuid skip, std::vector<DutyNeed>& needs)
    {
        DutyKey const handled{ DutyKeyKind::EnemyHandled, e.guid };
        DutyNeed need;
        need.kind = DutyKind::Intercept;
        need.target = e.guid;
        need.now = true;
        need.points = points;
        need.reason = reason;
        need.blockedBy.push_back(handled);
        need.marks.push_back(handled);
        need.tieMs = uint32(PackResolver::Distance(e.pos, tieTo) * 1000.0f);
        for (PartyMemberFacts const& m : world.party)
        {
            if (!m.isBot || !m.DealsDamage() || !m.CanAct() || m.guid == skip)
                continue;
            bool const ranged = m.role != ROLE_MELEE_DPS && m.role != ROLE_TANK;
            float const range = ranged ? FocusAssigner::INTERCEPT_RANGE : FocusAssigner::MELEE_INTERCEPT_RANGE;
            float const d = PackResolver::Distance(m.pos, e.pos);
            if (d > range)
                continue;
            DutyCandidate c;
            c.bot = m.guid;
            c.rank = (ranged ? KILL_NEED_RANGED_FIRST : 0.0f) - d;
            need.candidates.push_back(c);
        }
        if (!need.candidates.empty())
            needs.push_back(std::move(need));
    }

    bool WaveWatch::Update(WorldModel const& world)
    {
        bool fighting = !world.enemies.empty() || !world.allies.empty();
        for (PartyMemberFacts const& m : world.party)
            fighting = fighting || (!m.isPet && m.alive && m.inCombat);
        if (fighting || !lastFightMs)
            lastFightMs = world.refreshedAtMs;
        return fighting;
    }

    uint32 WaveWatch::QuietMs(uint32 nowMs) const
    {
        return lastFightMs ? WorldTimer::getMSTimeDiff(lastFightMs, nowMs) : 0;
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
