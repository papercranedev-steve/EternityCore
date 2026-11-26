#include "Commander/Behaviors/Constraints.h"
#include "Commander/Behaviors/Combat/AreaDamageBehavior.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/Spatial/PackResolver.h"
#include "Commander/Perception/Profiles/SpellTags.h"
#include "Commander/Perception/Profiles/Tags.h"
#include "Commander/Perception/Forecast/FinishForecast.h"
#include "Commander/Perception/Forecast/ThreatView.h"
#include "Commander/Perception/Values/DamageSpellValue.h"
#include "Commander/Perception/WorldModel.h"
#include "DamageSpells.h"
#include "Timer.h"
#include <algorithm>
#include "TeamBotAI.h"
#include "Map.h"
#include "SpellEntry.h"
#include "SpellMgr.h"
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

        // An enemy the plan keeps from area hits (TeamPlan::AreaKeepsUnbroken) within `radius`
        // of `centre` (its edge), `except` left out.
        bool KeptNear(WorldModel const& world, TeamPlan const& plan, ObjectGuid self, Position3 const& centre,
                      float radius, ObjectGuid except, uint32 nowMs)
        {
            for (EnemyFacts const& e : world.enemies)
            {
                if (!e.acting || e.guid == except || PackResolver::Distance(centre, e.pos) - e.boundingRadius > radius)
                    continue;
                if (plan.AreaKeepsUnbroken(e) && !plan.controlEnd.Breaks(self, e.guid, nowMs))
                    return true;
            }
            return false;
        }

        char const* BrokenByCast(TeamBotAI const* bot, WorldModel const& world, TeamPlan const& plan,
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
            if (char const* held = Constraints::LongCastHeld(bot, plan, spell))
                return held;

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
        char const* CastForbidden(TeamBotAI const* bot, WorldModel const& world, TeamPlan const& plan,
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
                // A neutral creature already in the party's fight is an enemy like any other: its
                // reaction stays neutral in combat (faction only), and the mage stood idle all
                // fight next to one the player attacked (strat-20261114-183556).
                if (target->IsCreature() && !me->IsHostileTo(target) && me->IsValidAttackTarget(target) &&
                    !world.FindEnemy(target->GetObjectGuid()))
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

        char const* LongCastHeld(TeamBotAI const* bot, TeamPlan const& plan, SpellEntry const* spell)
        {
            if (!spell || !plan.ForbidsLongCast(bot->me->GetObjectGuid()))
                return nullptr;
            if (spell->GetCastTime(bot->me) <= 0 && !spell->IsChanneledSpell())
                return nullptr;
            if (AreaDamageBehavior::Judges(bot, spell))
                return nullptr;
            return "reserved for the area spell";
        }

        char const* InterruptKept(TeamBotAI const* bot, TeamPlan const& plan, SpellEntry const* spell)
        {
            if (!spell)
                return nullptr;
            ObjectGuid const self = bot->me->GetObjectGuid();
            auto const blocks = [spell](uint32 keptId)
            {
                if (!keptId)
                    return false;
                if (keptId == spell->Id)
                    return true;
                if (!spell->Category || !spell->CategoryRecoveryTime)
                    return false;
                SpellEntry const* kept = sSpellMgr.GetSpellEntry(keptId);
                return kept && kept->Category == spell->Category;
            };
            for (InterruptDuty const& d : plan.interrupts)
                if ((d.primary == self && blocks(d.primarySpellId)) || (d.backup == self && blocks(d.backupSpellId)))
                    return "kept for an interrupt";
            for (DutyReserve const& r : plan.reserves)
                for (DutyReserve::Cover const& c : r.covers)
                    if (c.bot == self && blocks(c.spellId))
                        return "kept for a later interrupt";
            return nullptr;
        }

        char const* CastAtForbidden(TeamBotAI const* bot, WorldModel const& world, TeamPlan const& plan,
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

        bool MayBeWasted(TeamBotAI const* bot, SpellEntry const* spell)
        {
            if (!spell || spell->IsChanneledSpell() || spell->IsAutoRepeatRangedSpell() ||
                spell->IsAreaOfEffectSpell() || !SpellTags::DealsDamage(spell))
                return false;
            if (spell->GetCastTime(bot->me) > 0)
                return true;
            for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
                if (spell->Effect[i] == SPELL_EFFECT_APPLY_AURA &&
                    (spell->EffectApplyAuraName[i] == SPELL_AURA_PERIODIC_DAMAGE ||
                     spell->EffectApplyAuraName[i] == SPELL_AURA_PERIODIC_LEECH))
                    return true;
            return false;
        }

        char const* WastedCast(TeamBotAI const* bot, WorldModel const& world, Unit const* target,
                               SpellEntry const* spell, uint32 nowMs)
        {
            if (!MayBeWasted(bot, spell) || !target)
                return nullptr;
            Player* me = bot->me;
            EnemyFacts const* enemy = world.FindEnemy(target->GetObjectGuid());
            PartyMemberFacts const* self = world.FindMember(me->GetObjectGuid());
            if (!enemy || !self)
                return nullptr;
            int32 const castMs = std::max<int32>(0, int32(spell->GetCastTime(me)));
            // Facts are from the refresh: the hit lands this long after it.
            uint32 const sinceRefresh = WorldTimer::getMSTimeDiff(world.refreshedAtMs, nowMs);
            uint32 const landMs = sinceRefresh + uint32(castMs) + FinishForecast::FlightMs(self->pos, *enemy, spell->speed);
            if (castMs > 0 && FinishForecast::Finished(world, *enemy, self->guid, landMs))
                return "the others kill it before it lands";

            // A periodic spell pays with the ticks it gets in before the enemy dies; less than
            // the bot's own damage over the time it spends on it: something else is better.
            DamageSpells::Periodic const dot = DamageSpells::EstimatePeriodic(me, target, spell);
            if (!dot.ticks || !dot.periodMs || self->soloDps <= 0.0f)
                return nullptr;
            float const busySec = DamageSpells::BusySec(me, spell);
            uint32 const horizon = landMs + dot.ticks * dot.periodMs;
            // (the earlier death estimate: DamageSpellValue::DeathMs, strat-20261127-000359)
            uint32 const deathMs = DamageSpellValue::DeathMs(world, *enemy, self->guid,
                                                             sinceRefresh + uint32(busySec * 1000.0f), horizon);
            if (!deathMs)
                return nullptr;     // lives through the whole effect
            uint32 const ticks = deathMs > landMs ? std::min(dot.ticks, (deathMs - landMs) / dot.periodMs) : 0;
            float const worth = float(ticks) * dot.perTick +
                                (deathMs > landMs ? DamageSpells::EstimateDirect(me, target, spell) : 0.0f);
            if (worth < self->soloDps * busySec)
                return "it dies before the periodic damage pays";
            return nullptr;
        }

        char const* WakesControl(TeamBotAI const* bot, WorldModel const& world, TeamPlan const& plan,
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

        char const* Broken(TeamBotAI const* bot, WorldModel const& world, TeamPlan const& plan,
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
