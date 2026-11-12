#include "Commander/Behaviors/Constraints.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/Spatial/PackResolver.h"
#include "Commander/Perception/Profiles/SpellTags.h"
#include "Commander/Perception/Profiles/Tags.h"
#include "Commander/Perception/Forecast/ThreatView.h"
#include "Commander/Perception/WorldModel.h"
#include "PartyBotAI.h"
#include "Map.h"
#include "SpellEntry.h"
#include <cstring>

namespace Commander
{
    namespace
    {
        Position3 PositionOf(WorldModel const& world, Unit const* unit);

        // Damage on `enemy` would wake a control the plan keeps (not when this bot is sent to wake it).
        bool WakesKept(TeamPlan const& plan, ObjectGuid self, EnemyFacts const& enemy, uint32 nowMs)
        {
            return plan.KeepsUnbroken(enemy) && !plan.controlEnd.Breaks(self, enemy.guid, nowMs);
        }

        Position3 PositionOf(WorldModel const& world, Unit const* unit)
        {
            if (EnemyFacts const* e = world.FindEnemy(unit->GetObjectGuid()))
                return e->pos;
            Position3 p;
            p.x = unit->GetPositionX();
            p.y = unit->GetPositionY();
            p.z = unit->GetPositionZ();
            return p;
        }

        // An enemy the plan keeps within `radius` of `centre` (its edge), `except` left out.
        bool KeptNear(WorldModel const& world, TeamPlan const& plan, ObjectGuid self, Position3 const& centre,
                      float radius, ObjectGuid except, uint32 nowMs)
        {
            for (EnemyFacts const& e : world.enemies)
            {
                if (!e.acting || e.guid == except || PackResolver::Distance(centre, e.pos) - e.boundingRadius > radius)
                    continue;
                if (WakesKept(plan, self, e, nowMs))
                    return true;
            }
            return false;
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

            if (action.atPoint)
                return Constraints::CastAtForbidden(bot, world, plan, Position3{ action.x, action.y, action.z }, spell, nowMs);
            Unit const* target = action.target.IsEmpty() ? me : me->GetMap()->GetUnit(action.target);
            if (!target)
                return nullptr;
            return Constraints::CastForbidden(bot, world, plan, target, spell, nowMs);
        }

        // A neutral (yellow) creature within `radius` of `centre` (user 2026-11-10: never touch one).
        bool NeutralIn(WorldModel const& world, Position3 const& centre, float radius)
        {
            return Tags::NeutralNear(world, centre, radius) != nullptr;
        }
    }

    namespace Constraints
    {
        char const* CastForbidden(PartyBotAI const* bot, WorldModel const& world, TeamPlan const& plan,
                                  Unit const* target, SpellEntry const* spell, uint32 nowMs)
        {
            if (!spell || !target)
                return nullptr;
            Player* me = bot->me;
            // Never a neutral creature, not even at the edge of an area spell or a chained hit.
            if (float const splash = SpellTags::MeleeSplashRadius(spell))
            {
                Unit const* victim = me->GetVictim();
                if (victim && NeutralIn(world, PositionOf(world, victim), splash))
                    return "never a neutral creature (melee splash)";
            }
            else if (SpellTags::DealsDamage(spell))
            {
                if (target->IsCreature() && !me->IsHostileTo(target) && me->IsValidAttackTarget(target))
                    return "never a neutral creature";
                Position3 const centre = PositionOf(world, target);
                if (spell->IsAreaOfEffectSpell() && NeutralIn(world, centre, SpellTags::AreaRadius(spell)))
                    return "never a neutral creature (area)";
                if (SpellTags::ChainsToNearby(spell) && NeutralIn(world, centre, SpellTags::CHAIN_JUMP_YD))
                    return "never a neutral creature (chained hit)";
                // Its damage shield / reflection is up (RoleStamps.h HoldFire).
                if (plan.doctrine.Has(target->GetObjectGuid(), STAMP_HOLD_FIRE))
                    return "it reflects damage: hands off";
            }
            return WakesControl(bot, world, plan, target, spell, nowMs);
        }

        char const* CastAtForbidden(PartyBotAI const* bot, WorldModel const& world, TeamPlan const& plan,
                                    Position3 const& centre, SpellEntry const* spell, uint32 nowMs)
        {
            if (!spell || !SpellTags::DealsDamage(spell))
                return nullptr;
            float const radius = SpellTags::AreaRadius(spell);
            if (NeutralIn(world, centre, radius))
                return "never a neutral creature (area)";
            if (KeptNear(world, plan, bot->me->GetObjectGuid(), centre, radius, ObjectGuid(), nowMs))
                return "keep the control unbroken (area)";
            return nullptr;
        }

        char const* WakesControl(PartyBotAI const* bot, WorldModel const& world, TeamPlan const& plan,
                                 Unit const* target, SpellEntry const* spell, uint32 nowMs)
        {
            if (!spell || !target)
                return nullptr;
            Player* me = bot->me;
            ObjectGuid const self = me->GetObjectGuid();

            // A melee splash buff: its hits on the victim jump to an enemy next to it.
            if (float const splash = SpellTags::MeleeSplashRadius(spell))
            {
                Unit const* victim = me->GetVictim();
                if (victim && KeptNear(world, plan, self, PositionOf(world, victim), splash, victim->GetObjectGuid(), nowMs))
                    return "keep the control unbroken (melee splash)";
                return nullptr;
            }

            if (!SpellTags::DealsDamage(spell))
                return nullptr;
            EnemyFacts const* facts = world.FindEnemy(target->GetObjectGuid());
            if (facts && WakesKept(plan, self, *facts, nowMs))
                return "keep the control unbroken";
            Position3 const centre = PositionOf(world, target);
            if (spell->IsAreaOfEffectSpell() &&
                KeptNear(world, plan, self, centre, SpellTags::AreaRadius(spell), ObjectGuid(), nowMs))
                return "keep the control unbroken (area)";
            if (SpellTags::ChainsToNearby(spell) &&
                KeptNear(world, plan, self, centre, SpellTags::CHAIN_JUMP_YD, target->GetObjectGuid(), nowMs))
                return "keep the control unbroken (chained hit)";
            return nullptr;
        }

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
