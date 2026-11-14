/*
 * Shaman rotation (moved out of PartyBotAI.cpp 2026-11-14, same steps).
 *
 * Out of combat: weapon buff, Lightning Shield, heals (healer); with a victim (an opener):
 * totems, then the combat steps.
 *
 * In combat:
 *   1. Not a healer: Frost Shock on a moving victim, Stormstrike, Chain Lightning, Purge,
 *      Flame Shock, Lightning Bolt (ranged role, or the victim out of melee reach).
 *   2. Totems (CombatBotBaseAI::SummonShamanTotems).
 *   3. Healer: heals (the Commander heal module while the party fights). Others: a self heal
 *      below 20%.
 *
 * The Commander decides: Earth Shock on a cast (InterruptBehavior), Mana Tide (BoostBehavior),
 * Elemental Mastery (BurstBehavior), Nature's Swiftness (heal boosts), dispels in combat.
 * Still the rotation's own: Purge (no Commander module for enemy buffs yet).
 */

#include "Rotations/ClassRotation.h"
#include "PartyBotAI.h"
#include "Player.h"

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
                if (bot.SummonShamanTotems())
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

            if (bot.SummonShamanTotems())
                return;

            if (bot.GetRole() == ROLE_HEALER)
            {
                if (bot.FindAndHealInjuredAlly(50.0f, 90.0f))
                    return;
                bot.FindAndPreHealTarget();
            }
            else if (me->GetHealthPercent() < 20.0f)
                bot.HealInjuredTarget(me);
        }

    private:
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
