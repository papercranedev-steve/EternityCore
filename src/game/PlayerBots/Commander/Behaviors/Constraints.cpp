#include "Commander/Behaviors/Constraints.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/PackResolver.h"
#include "Commander/Perception/SpellTags.h"
#include "Commander/Perception/WorldModel.h"
#include "PartyBotAI.h"
#include "Map.h"
#include "SpellEntry.h"
#include <cstring>

namespace Commander
{
    namespace
    {
        // Damage on `enemy` would wake a control the plan keeps (not when this bot is sent to wake it).
        bool WakesKept(TeamPlan const& plan, ObjectGuid self, EnemyFacts const& enemy, uint32 nowMs)
        {
            return plan.KeepsUnbroken(enemy) && !plan.controlEnd.Breaks(self, enemy.guid, nowMs);
        }

        char const* BrokenByCast(PartyBotAI const* bot, WorldModel const& world, TeamPlan const& plan,
                                 BotAction const& action, char const* source, uint32 nowMs)
        {
            SpellEntry const* spell = action.spell;
            if (!spell)
                return nullptr;
            Player* me = bot->me;
            ObjectGuid const self = me->GetObjectGuid();

            if (spell->HasEffect(SPELL_EFFECT_CHARGE) && plan.ForbidsGapCloser(self))
                return "no charge (pull)";
            // Kept for an enemy about to run away (FleeStopBehavior casts it).
            if (strcmp(source, "fleestop") && plan.HoldsSpell(self, spell->Id, spell->NeedsComboPoints()))
                return "kept for a flee stop";

            if (!SpellTags::DealsDamage(spell))
                return nullptr;
            EnemyFacts const* target = world.FindEnemy(action.target);
            if (target && WakesKept(plan, self, *target, nowMs))
                return "keep the control unbroken";
            if (!spell->IsAreaOfEffectSpell())
                return nullptr;

            Position3 centre;
            if (target)
                centre = target->pos;
            else if (Unit const* unit = action.target.IsEmpty() ? me : me->GetMap()->GetUnit(action.target))
            {
                centre.x = unit->GetPositionX();
                centre.y = unit->GetPositionY();
                centre.z = unit->GetPositionZ();
            }
            else
                return nullptr;
            float const radius = SpellTags::AreaRadius(spell);
            for (EnemyFacts const& e : world.enemies)
            {
                if (!e.acting || PackResolver::Distance(centre, e.pos) - e.boundingRadius > radius)
                    continue;
                if (WakesKept(plan, self, e, nowMs))
                    return "keep the control unbroken (area)";
            }
            return nullptr;
        }
    }

    namespace Constraints
    {
        char const* Broken(PartyBotAI const* bot, WorldModel const& world, TeamPlan const& plan,
                           Proposal const& p, uint32 nowMs)
        {
            if (p.kind != ProposalKind::Replace)
                return nullptr;
            ObjectGuid const self = bot->me->GetObjectGuid();
            switch (p.action.type)
            {
                case BotActionType::Attack:
                case BotActionType::Chase:
                case BotActionType::PetAttack:
                    return plan.EngageBlock(self, p.action.target);
                case BotActionType::Cast:
                    return BrokenByCast(bot, world, plan, p.action, p.source, nowMs);
                case BotActionType::Rest:
                    return plan.ForbidsRest(self) ? "no rest" : nullptr;
                default:
                    return nullptr;
            }
        }
    }
}
