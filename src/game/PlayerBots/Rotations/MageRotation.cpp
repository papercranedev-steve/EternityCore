#include "Rotations/ClassRotation.h"
#include "PartyBotAI.h"
#include "Player.h"
#include "MotionMaster.h"
#include "Spell.h"

namespace
{
    class MageRotation : public ClassRotation
    {
    public:
        using ClassRotation::ClassRotation;

        void UpdateOutOfCombat() override
        {
            PartyBotAI& bot = m_bot;
            Player* me = bot.me;
            auto const& s = bot.m_spells.mage;

            SpellEntry const* pBuffSpell = nullptr;
            if (Player* pTarget = bot.SelectBuffTarget(s.pArcaneIntellect, s.pArcaneBrilliance, pBuffSpell))
            {
                if (bot.CanTryToCastSpell(pTarget, pBuffSpell))
                {
                    if (bot.DoCastSpell(pTarget, pBuffSpell) == SPELL_CAST_OK)
                    {
                        bot.m_isBuffing = true;
                        me->ClearTarget();
                        return;
                    }
                }
            }

            if (s.pIceArmor &&
                bot.CanTryToCastSpell(me, s.pIceArmor))
            {
                if (bot.DoCastSpell(me, s.pIceArmor) == SPELL_CAST_OK)
                {
                    bot.m_isBuffing = true;
                    me->ClearTarget();
                    return;
                }
            }

            if (s.pIceBarrier &&
                bot.CanTryToCastSpell(me, s.pIceBarrier))
            {
                if (bot.DoCastSpell(me, s.pIceBarrier) == SPELL_CAST_OK)
                {
                    bot.m_isBuffing = true;
                    me->ClearTarget();
                    return;
                }
            }

            if (bot.m_isBuffing &&
               (!s.pArcaneIntellect ||
                !me->HasGCD(s.pArcaneIntellect)))
            {
                bot.m_isBuffing = false;
            }

            if (me->GetVictim())
                UpdateInCombat();
        }

        void UpdateInCombat() override
        {
            PartyBotAI& bot = m_bot;
            Player* me = bot.me;
            auto const& s = bot.m_spells.mage;

            Unit* pVictim = me->GetVictim();
            if (!pVictim)
                return;

            if (s.pCombustion &&
                bot.CanTryToCastSpell(me, s.pCombustion))
            {
                if (bot.DoCastSpell(me, s.pCombustion) == SPELL_CAST_OK)
                    return;
            }

            if (s.pPyroblast &&
               ((s.pPresenceOfMind && me->HasAura(s.pPresenceOfMind->Id)) ||
                (!pVictim->IsInCombat() && (pVictim->GetMaxHealth() > me->GetMaxHealth()) && (me->GetDistance(pVictim) > 30.0f))) &&
                bot.CanTryToCastSpell(pVictim, s.pPyroblast))
            {
                if (bot.DoCastSpell(pVictim, s.pPyroblast) == SPELL_CAST_OK)
                    return;
            }

            if (s.pIceBlock &&
               (me->GetHealthPercent() < 10.0f) &&
                bot.CanTryToCastSpell(me, s.pIceBlock))
            {
                if (bot.DoCastSpell(me, s.pIceBlock) == SPELL_CAST_OK)
                    return;
            }

            if (me->GetMotionMaster()->GetCurrentMovementGeneratorType() == IDLE_MOTION_TYPE
                && me->GetDistance(pVictim) > 30.0f)
            {
                bot.BeginChasing(pVictim);
            }
            else if (bot.GetAttackersInRangeCount(10.0f))
            {
                if (s.pManaShield &&
                   (me->GetPowerPercent(POWER_MANA) > 20.0f) &&
                    bot.CanTryToCastSpell(me, s.pManaShield))
                {
                    if (bot.DoCastSpell(me, s.pManaShield) == SPELL_CAST_OK)
                        return;
                }

                // Commander escape module on: it decides Frost Nova (only when the hits are dangerous)
                // and runs to the tank (Commander/Behaviors/EscapeBehavior.h).
                if ((bot.GetRole() != ROLE_MELEE_DPS) && !bot.IsEscapeControlled() &&
                    (me->GetMotionMaster()->GetCurrentMovementGeneratorType() != DISTANCING_MOTION_TYPE))
                {
                    if (s.pBlink &&
                        (me->HasUnitState(UNIT_STATE_CAN_NOT_MOVE) ||
                            me->HasAuraType(SPELL_AURA_MOD_DECREASE_SPEED)) &&
                        bot.CanTryToCastSpell(me, s.pBlink))
                    {
                        if (me->GetMotionMaster()->GetCurrentMovementGeneratorType())
                            me->GetMotionMaster()->Clear();

                        if (bot.DoCastSpell(me, s.pBlink) == SPELL_CAST_OK)
                            return;
                    }

                    if (!me->HasUnitState(UNIT_STATE_CAN_NOT_MOVE))
                    {
                        if (s.pFrostNova &&
                           !pVictim->HasUnitState(UNIT_STATE_ROOT) &&
                           !pVictim->HasUnitState(UNIT_STATE_CAN_NOT_REACT_OR_LOST_CONTROL) &&
                            bot.CanTryToCastSpell(me, s.pFrostNova))
                        {
                            bot.DoCastSpell(me, s.pFrostNova);
                        }

                        if (bot.RunAwayFromTarget(pVictim))
                        {
                            me->SetCasterChaseDistance(25.0f);
                            return;
                        }
                    }
                }
            }

            if (me->GetEnemyCountInRadiusAround(me, 10.0f) > 1)
            {
                if (s.pConeofCold && !me->IsMoving() &&
                    bot.CanTryToCastSpell(me, s.pConeofCold))
                {
                    if (bot.DoCastSpell(pVictim, s.pConeofCold) == SPELL_CAST_OK)
                        return;
                }

                if (s.pBlastWave &&
                    bot.CanTryToCastSpell(me, s.pBlastWave))
                {
                    if (bot.DoCastSpell(me, s.pBlastWave) == SPELL_CAST_OK)
                        return;
                }

                if (s.pArcaneExplosion &&
                    bot.CanTryToCastSpell(me, s.pArcaneExplosion))
                {
                    if (bot.DoCastSpell(me, s.pArcaneExplosion) == SPELL_CAST_OK)
                        return;
                }
            }

            if (me->GetMotionMaster()->GetCurrentMovementGeneratorType() == DISTANCING_MOTION_TYPE)
                return;

            if (s.pCounterspell &&
                pVictim->IsNonMeleeSpellCasted(false, false, true) && !bot.IsInterruptVetoed(pVictim) &&
                bot.CanTryToCastSpell(pVictim, s.pCounterspell))
            {
                if (bot.DoCastSpell(pVictim, s.pCounterspell) == SPELL_CAST_OK)
                    return;
            }

            if (s.pBlizzard &&
               (me->GetEnemyCountInRadiusAround(pVictim, 10.0f) > 2) &&
                bot.CanTryToCastSpell(pVictim, s.pBlizzard))
            {
                if (bot.DoCastSpell(pVictim, s.pBlizzard) == SPELL_CAST_OK)
                    return;
            }

            if (s.pPolymorph)
            {
                if (Unit* pTarget = bot.SelectAttackerDifferentFrom(pVictim))
                {
                    if (pTarget->GetHealthPercent() > 20.0f &&
                        bot.CanTryToCastSpell(pTarget, s.pPolymorph) &&
                        bot.CanUseCrowdControl(s.pPolymorph, pTarget))
                    {
                        if (bot.DoCastSpell(pTarget, s.pPolymorph) == SPELL_CAST_OK)
                            return;
                    }
                }
            }

            if (s.pArcanePower &&
                (me->GetPowerPercent(POWER_MANA) > 50.0f) &&
                bot.CanTryToCastSpell(me, s.pArcanePower))
            {
                if (bot.DoCastSpell(me, s.pArcanePower) == SPELL_CAST_OK)
                    return;
            }

            if (s.pPresenceOfMind &&
               (me->GetPowerPercent(POWER_MANA) > 50.0f) &&
                bot.CanTryToCastSpell(me, s.pPresenceOfMind))
            {
                if (bot.DoCastSpell(me, s.pPresenceOfMind) == SPELL_CAST_OK)
                    return;
            }

            if (s.pScorch &&
               (pVictim->GetHealthPercent() < 20.0f) &&
                bot.CanTryToCastSpell(pVictim, s.pScorch))
            {
                if (bot.DoCastSpell(pVictim, s.pScorch) == SPELL_CAST_OK)
                    return;
            }

            if (s.pFrostbolt &&
                bot.CanTryToCastSpell(pVictim, s.pFrostbolt))
            {
                if (bot.DoCastSpell(pVictim, s.pFrostbolt) == SPELL_CAST_OK)
                    return;
            }

            if (s.pFireBlast &&
                bot.CanTryToCastSpell(pVictim, s.pFireBlast))
            {
                if (bot.DoCastSpell(pVictim, s.pFireBlast) == SPELL_CAST_OK)
                    return;
            }

            if (s.pFireball &&
                bot.CanTryToCastSpell(pVictim, s.pFireball))
            {
                if (bot.DoCastSpell(pVictim, s.pFireball) == SPELL_CAST_OK)
                    return;
            }

            if (s.pEvocation &&
               (me->GetPowerPercent(POWER_MANA) < 30.0f) &&
               (bot.GetAttackersInRangeCount(10.0f) == 0) &&
                bot.CanTryToCastSpell(me, s.pEvocation))
            {
                if (bot.DoCastSpell(me, s.pEvocation) == SPELL_CAST_OK)
                    return;
            }

            if (me->HasSpell(ClassRotations::SPELL_SHOOT_WAND) &&
               !me->IsMoving() &&
               (me->GetPowerPercent(POWER_MANA) < 5.0f) &&
               !me->GetCurrentSpell(CURRENT_AUTOREPEAT_SPELL))
                me->CastSpell(pVictim, ClassRotations::SPELL_SHOOT_WAND, false);
        }
    };
}

std::unique_ptr<ClassRotation> ClassRotations::MakeMage(PartyBotAI& bot)
{
    return std::make_unique<MageRotation>(bot);
}
