#include "Commander/Behaviors/Arbiter.h"
#include "Commander/BotDirectives.h"
#include "Commander/CommanderMgr.h"
#include "PartyBotAI.h"
#include "Map.h"
#include "MotionMaster.h"
#include "SpellEntry.h"
#include "Timer.h"
#include <algorithm>
#include <cstdio>

namespace Commander
{
    namespace
    {
        char const* ActionName(BotActionType type)
        {
            switch (type)
            {
                case BotActionType::Cast:   return "cast";
                case BotActionType::Attack: return "attack";
                case BotActionType::MoveTo: return "move";
                case BotActionType::Hold:   return "hold";
                case BotActionType::HoldFire: return "holdfire";
            }
            return "?";
        }

        std::string Describe(Proposal const& p)
        {
            char buf[160];
            snprintf(buf, sizeof(buf), "%s tier%u score%.2f %s spell%u",
                p.source, uint32(p.tier), p.score, ActionName(p.action.type),
                p.action.spell ? p.action.spell->Id : 0);
            return buf;
        }
    }

    bool Arbiter::Run(PartyBotAI* bot, WorldModel const& world, TeamPlan const& plan,
                      BehaviorList const& behaviors)
    {
        std::vector<IBehavior const*> active;
        for (auto const& slot : behaviors)
            if (slot->enabled.load(std::memory_order_relaxed) && slot->module->AppliesTo(bot))
                active.push_back(slot->module.get());

        if (active.empty())
            return false;

        uint32 const now = WorldTimer::getMSTime();

        // Keeps the original rotation's own taunt switched off while a module decides taunts.
        // Expires by itself if the module is disabled or the Commander is turned off.
        for (IBehavior const* b : active)
        {
            if (b->ControlsTaunt(bot))
            {
                bot->Directives().ControlTaunt(now, 3 * sCommanderMgr.GetBotUpdateInterval());
                break;
            }
        }

        std::vector<Proposal> proposals;
        for (IBehavior const* b : active)
            b->Propose(bot, world, plan, proposals);

        if (proposals.empty())
            return false;

        for (IBehavior const* b : active)
            b->Adjust(bot, world, proposals);

        std::stable_sort(proposals.begin(), proposals.end(), [](Proposal const& a, Proposal const& b)
        {
            if (a.tier != b.tier)
                return a.tier < b.tier;
            return a.score > b.score;
        });

        ProposalTier const topTier = proposals.front().tier;
        for (Proposal const& p : proposals)
        {
            // Lower tiers are only considered when nothing in the top tier exists,
            // not when the top tier failed to execute.
            if (p.tier != topTier)
                break;

            if (Execute(bot, p))
            {
                bot->Directives().SetLastDecision(now, Describe(p));
                return true;
            }
        }

        bot->Directives().SetLastDecision(now, "failed: " + Describe(proposals.front()));
        return false;
    }

    bool Arbiter::Execute(PartyBotAI* bot, Proposal const& proposal)
    {
        Player* me = bot->me;
        BotAction const& action = proposal.action;

        Unit* pTarget = nullptr;
        if (!action.target.IsEmpty())
        {
            pTarget = me->GetMap()->GetUnit(action.target);
            if (!pTarget || !pTarget->IsInWorld() || !pTarget->IsAlive())
                return false;
        }

        switch (action.type)
        {
            case BotActionType::Cast:
            {
                if (!action.spell)
                    return false;
                Unit* pCastTarget = pTarget ? pTarget : me;
                if (!bot->CanTryToCastSpell(pCastTarget, action.spell))
                    return false;
                return bot->DoCastSpell(pCastTarget, action.spell) == SPELL_CAST_OK;
            }
            case BotActionType::Attack:
            {
                if (!pTarget || me->GetVictim() == pTarget || !bot->IsValidHostileTarget(pTarget))
                    return false;
                if (me->IsMounted())    // Unit::Attack refuses while mounted
                    me->RemoveSpellsCausingAura(SPELL_AURA_MOUNTED);
                return bot->AttackStart(pTarget);
            }
            case BotActionType::MoveTo:
            {
                if (me->HasUnitState(UNIT_STATE_ROOT))
                    return false;
                me->GetMotionMaster()->MovePoint(0, action.x, action.y, action.z, MOVE_PATHFINDING);
                bot->Directives().HoldMovement(WorldTimer::getMSTime(), action.holdMs);
                return true;
            }
            case BotActionType::Hold:
                return true;
            case BotActionType::HoldFire:
            {
                if (me->GetCurrentSpell(CURRENT_AUTOREPEAT_SPELL))
                    me->InterruptSpell(CURRENT_AUTOREPEAT_SPELL, true);
                if (me->HasUnitState(UNIT_STATE_MELEE_ATTACKING))
                {
                    // Keep the victim so the original AI resumes on it (AttackStart re-enables melee).
                    me->ClearUnitState(UNIT_STATE_MELEE_ATTACKING);
                    if (Unit* pVictim = me->GetVictim())
                        me->SendMeleeAttackStop(pVictim);
                }
                return true;
            }
        }

        return false;
    }
}
