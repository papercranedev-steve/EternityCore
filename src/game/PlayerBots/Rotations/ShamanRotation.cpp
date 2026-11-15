/*
 * Shaman rotation (moved out of PartyBotAI.cpp 2026-11-14, same steps).
 *
 * Out of combat: weapon buff, Lightning Shield, heals (healer); with a victim (an opener):
 * totems, then the combat steps.
 *
 * In combat:
 *   1. Not a healer: Frost Shock on a moving victim, Stormstrike, Chain Lightning, Purge,
 *      Flame Shock, Lightning Bolt (ranged role, or the victim out of melee reach).
 *   2. Healer: heals (the Commander heal module while the party fights), then totems, then a
 *      pre-heal. Others: totems, then a self heal below 20%.
 *
 * Totems (KeepTotems): the brain's choice per element (Coordination/Duties/TotemAssigner.h),
 * TotemAssigner::Order for a party with melee and mana users while the module is off. One is
 * dropped when its element is empty, holds another totem or was left behind
 * (CombatBotBaseAI::KeepShamanTotem).
 *
 * The Commander decides: Earth Shock on a cast (InterruptBehavior), Mana Tide (BoostBehavior),
 * Elemental Mastery (BurstBehavior), Nature's Swiftness (heal boosts), dispels in combat.
 * Still the rotation's own: Purge (no Commander module for enemy buffs yet).
 */

#include "Rotations/ClassRotation.h"
#include "PartyBotAI.h"
#include "Player.h"
#include "SpellMgr.h"
#include "Commander/CommanderMgr.h"
#include "Commander/PartyCommander.h"
#include "Commander/Coordination/Duties/TotemAssigner.h"

namespace
{
    class ShamanRotation : public ClassRotation
    {
    public:
        using ClassRotation::ClassRotation;

        void UpdateOutOfCombat() override
        {
            PartyBotAI& bot = m_bot;
            Player* me = bot.me;
            auto const& s = bot.m_spells.shaman;

            if (s.pWeaponBuff && bot.CanTryToCastSpell(me, s.pWeaponBuff) &&
                bot.CastWeaponBuff(s.pWeaponBuff, EQUIPMENT_SLOT_MAINHAND) == SPELL_CAST_OK)
                return;

            if (Cast(me, s.pLightningShield))
                return;

            if (bot.m_role == ROLE_HEALER && bot.FindAndHealInjuredAlly())
                return;

            if (me->GetVictim())
            {
                if (KeepTotems())
                    return;
                UpdateInCombat();
            }
        }

        void UpdateInCombat() override
        {
            PartyBotAI& bot = m_bot;
            Player* me = bot.me;

            if (bot.GetRole() != ROLE_HEALER)
                if (Unit* pVictim = me->GetVictim())
                    if (Damage(pVictim))
                        return;

            // A healer heals before it drops totems: up to four totems are four GCDs without a
            // heal (until 2026-11-15 totems came first, also at the pull and after a totem died).
            // The heal module orders nothing while nobody needs a lift (rest): the totems go
            // down in those gaps.
            if (bot.GetRole() == ROLE_HEALER)
            {
                if (bot.FindAndHealInjuredAlly(50.0f, 90.0f))
                    return;
                if (KeepTotems())
                    return;
                bot.FindAndPreHealTarget();
                return;
            }

            if (KeepTotems())
                return;

            if (me->GetHealthPercent() < 20.0f)
                bot.HealInjuredTarget(me);
        }

    private:
        // The brain's totem plan, nullptr while the module (or the Commander) is off.
        std::shared_ptr<Commander::PartyCommander> PlanOwner() const
        {
            if (!sCommanderMgr.IsEnabled())
                return nullptr;
            std::shared_ptr<Commander::PartyCommander> commander = sCommanderMgr.FindCommander(m_bot.me);
            return commander && commander->GetPlan().totems.active ? commander : nullptr;
        }

        SpellEntry const* KnownTotem(Commander::TotemKind kind) const
        {
            auto const& s = m_bot.m_spells.shaman;
            switch (kind)
            {
                case Commander::TotemKind::ManaSpring:      return s.pManaSpringTotem;
                case Commander::TotemKind::HealingStream:   return s.pHealingStreamTotem;
                case Commander::TotemKind::StrengthOfEarth: return s.pStrengthOfEarthTotem;
                case Commander::TotemKind::Stoneskin:       return s.pStoneskinTotem;
                case Commander::TotemKind::Windfury:        return s.pWindfuryTotem;
                case Commander::TotemKind::GraceOfAir:      return s.pGraceOfAirTotem;
                case Commander::TotemKind::TranquilAir:     return s.pTranquilAirTotem;
                case Commander::TotemKind::Flametongue:     return s.pFlametongueTotem;
                default:                                    return nullptr;
            }
        }

        // Module off: the first of the element's list I know.
        SpellEntry const* OwnTotemChoice(TotemSlot slot) const
        {
            Commander::TotemAssigner::PartyMix mix;
            mix.melee = 1;
            mix.manaUsers = 1;
            for (Commander::TotemKind kind : Commander::TotemAssigner::Order(slot, mix))
                if (SpellEntry const* spell = KnownTotem(kind))
                    return spell;
            return nullptr;
        }

        // One totem dropped (true), elements in the order water, earth, air, fire.
        bool KeepTotems()
        {
            PartyBotAI& bot = m_bot;
            std::shared_ptr<Commander::PartyCommander> const owner = PlanOwner();
            static TotemSlot const SLOTS[] = { TOTEM_SLOT_WATER, TOTEM_SLOT_EARTH, TOTEM_SLOT_AIR, TOTEM_SLOT_FIRE };
            for (TotemSlot slot : SLOTS)
            {
                SpellEntry const* spell = nullptr;
                if (owner)
                {
                    if (uint32 const spellId = owner->GetPlan().totems.TotemFor(bot.me->GetObjectGuid(), slot))
                        spell = sSpellMgr.GetSpellEntry(spellId);
                }
                else
                    spell = OwnTotemChoice(slot);
                if (bot.KeepShamanTotem(slot, spell))
                    return true;
            }
            return false;
        }

        bool Damage(Unit* pVictim)
        {
            PartyBotAI& bot = m_bot;
            auto const& s = bot.m_spells.shaman;

            if (pVictim->IsMoving() && Cast(pVictim, s.pFrostShock))
                return true;
            if (Cast(pVictim, s.pStormstrike))
                return true;
            if (Cast(pVictim, s.pChainLightning))
                return true;
            if (s.pPurge && bot.IsValidDispelTarget(pVictim, s.pPurge) && Cast(pVictim, s.pPurge))
                return true;
            if (Cast(pVictim, s.pFlameShock))
                return true;
            if ((bot.GetRole() == ROLE_RANGE_DPS || !bot.me->CanReachWithMeleeAutoAttack(pVictim)) &&
                Cast(pVictim, s.pLightningBolt))
                return true;
            return false;
        }
    };
}

std::unique_ptr<ClassRotation> ClassRotations::MakeShaman(PartyBotAI& bot)
{
    return std::make_unique<ShamanRotation>(bot);
}
