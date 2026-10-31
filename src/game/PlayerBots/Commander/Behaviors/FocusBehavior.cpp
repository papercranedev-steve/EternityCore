#include "Commander/Behaviors/FocusBehavior.h"
#include "Commander/Behaviors/ThreatBehavior.h"
#include "Commander/Coordination/FocusAssigner.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/WorldModel.h"
#include "Commander/Perception/ThreatView.h"
#include "Commander/Perception/ValueModel.h"
#include "Commander/CommanderMgr.h"
#include "PartyBotAI.h"
#include "Group.h"
#include "Map.h"
#include <algorithm>

namespace Commander
{
    namespace
    {
        // Same as ThreatBehavior: somebody whose job is holding enemies (tank bot, or the player).
        bool IsHolder(PartyMemberFacts const& m)
        {
            if (m.isPet)
                return false;
            return !m.isBot || m.role == ROLE_TANK;
        }

        // A player focus mark the bot is configured to follow (`.partybot focusmark`) is on a
        // live enemy: the player chose the kill order, the original target selection follows it.
        bool HasLiveFocusMark(PartyBotAI const* bot)
        {
            Group const* group = bot->me->GetGroup();
            if (!group)
                return false;
            for (RaidTargetIcon mark : bot->m_marksToFocus)
            {
                ObjectGuid const guid = group->GetTargetWithIcon(mark);
                if (!guid.IsUnit())
                    continue;
                if (Unit const* unit = bot->me->GetMap()->GetUnit(guid))
                    if (unit->IsAlive())
                        return true;
            }
            return false;
        }

        // Threat I add per second: best rate measured on any enemy, else a level-based guess.
        float OwnThreatRate(WorldModel const& world, PartyMemberFacts const& me)
        {
            float best = 0.0f;
            for (EnemyFacts const& e : world.enemies)
                if (ThreatEntry const* t = ThreatView::FindEntry(e, me.guid))
                    best = std::max(best, t->threatPerSec);
            return best > 0.0f ? best : Value::FALLBACK_DPS_PER_LEVEL * float(me.level);
        }

        Proposal MakeAttack(ProposalTier tier, ObjectGuid target, char const* reason)
        {
            Proposal p;
            p.tier = tier;
            p.source = "focus";
            p.reason = reason;
            p.action.type = BotActionType::Attack;
            p.action.target = target;
            return p;
        }
    }

    bool FocusBehavior::AppliesTo(PartyBotAI const* bot) const
    {
        CombatBotRoles const role = bot->GetRole();
        return role == ROLE_MELEE_DPS || role == ROLE_RANGE_DPS;
    }

    void FocusBehavior::Propose(PartyBotAI const* bot, WorldModel const& world,
                                TeamPlan const& plan, std::vector<Proposal>& out) const
    {
        if (world.enemies.empty())
            return;
        PartyMemberFacts const* myFacts = world.FindMember(bot->me->GetObjectGuid());
        if (!myFacts || !myFacts->CanAct())
            return;

        // An interceptor stays on its enemy; the party focus is for everyone else.
        if (plan.FindInterceptFor(myFacts->guid))
            ProposeIntercept(bot, world, plan, out);
        else
            ProposeFocus(bot, world, plan, out);
    }

    void FocusBehavior::ProposeIntercept(PartyBotAI const* bot, WorldModel const& world,
                                         TeamPlan const& plan, std::vector<Proposal>& out) const
    {
        Player* me = bot->me;
        PartyMemberFacts const* myFacts = world.FindMember(me->GetObjectGuid());
        InterceptDuty const* duty = plan.FindInterceptFor(me->GetObjectGuid());
        if (!myFacts || !duty)
            return;
        Unit* enemy = me->GetMap()->GetUnit(duty->enemy);
        if (!enemy || !enemy->IsAlive() || me->GetVictim() == enemy || !bot->IsValidHostileTarget(enemy))
            return;

        // Urgent: a bomb reaching the melee pile or an add on the healer does not wait for the
        // team-task tier. Player focus marks do not stop it (it never moves the party focus).
        Proposal p = MakeAttack(ProposalTier::Urgent, duty->enemy, duty->reason);
        p.SetValue(duty->points, Value::LostAction(world, *myFacts, ThreatBehavior::SWITCH_TARGET_SEC));
        out.push_back(p);
    }

    void FocusBehavior::ProposeFocus(PartyBotAI const* bot, WorldModel const& world,
                                     TeamPlan const& plan, std::vector<Proposal>& out) const
    {
        if (plan.focus.enemy.IsEmpty())
            return;
        Player* me = bot->me;
        ObjectGuid const self = me->GetObjectGuid();
        Unit* victim = me->GetVictim();
        if (victim && victim->GetObjectGuid() == plan.focus.enemy)
            return;
        if (HasLiveFocusMark(bot))
            return;

        PartyMemberFacts const* myFacts = world.FindMember(self);
        EnemyFacts const* focus = world.FindEnemy(plan.focus.enemy);
        Unit* unit = me->GetMap()->GetUnit(plan.focus.enemy);
        if (!myFacts || !focus || !unit || !unit->IsAlive() || !bot->IsValidHostileTarget(unit))
            return;

        // Reach: ranged anywhere within FOCUS_RANGE in sight; melee only when it is close.
        if (!me->IsWithinDistInMap(unit, FOCUS_RANGE) || !me->IsWithinLOSInMap(unit))
            return;
        float walkSec = 0.0f;
        if (bot->GetRole() == ROLE_MELEE_DPS && !me->CanReachWithMeleeAutoAttack(unit))
        {
            if (!me->IsWithinDistInMap(unit, MELEE_FOCUS_RANGE))
                return;
            walkSec = std::max(0.0f, me->GetDistance(unit) - MELEE_REACH) / RUN_YARDS_PER_SEC;
        }

        // Threat: do not switch onto an enemy where ThreatBehavior would make us stop at once
        // (same rule as ThreatBehavior::ProposeDamage, fresh enemies are never the focus).
        if (focus->victim != self)
        {
            float const headroom = ThreatView::Headroom(*focus, self);
            if (headroom < 0.0f)
                return;
            PartyMemberFacts const* holder = world.FindMember(focus->victim);
            if (holder && IsHolder(*holder))
            {
                float const tickSec = sCommanderMgr.GetBotUpdateInterval() / 1000.0f;
                float const needed = std::max(ThreatBehavior::DPS_STOP_HEADROOM * focus->victimThreat,
                                              OwnThreatRate(world, *myFacts) * tickSec);
                if (headroom < needed)
                    return;
            }
        }

        // Exchange argument (FocusAssigner.h): killing the focus before the current target
        // saves w_f * t_c - w_c * t_f points when the whole party does it; this bot brings its
        // share of the party's damage. No current enemy: the focus' harm over its kill time.
        FocusScore const f = FocusAssigner::Evaluate(world, *focus);
        EnemyFacts const* current = victim ? world.FindEnemy(victim->GetObjectGuid()) : nullptr;
        float saved = f.harmRate * f.killSec;
        if (current)
        {
            FocusScore const c = FocusAssigner::Evaluate(world, *current);
            saved = f.harmRate * c.killSec - c.harmRate * f.killSec;
        }
        float const share = 1.0f / float(std::max(1u, world.rates.damageDealers));

        Proposal p = MakeAttack(ProposalTier::Assigned, plan.focus.enemy, "switch to focus");
        p.SetValue(saved * share,
                   Value::LostAction(world, *myFacts, ThreatBehavior::SWITCH_TARGET_SEC + walkSec));
        out.push_back(p);
    }
}
