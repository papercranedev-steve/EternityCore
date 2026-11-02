/*
 * Mage rotation (user, 2026-11-01, after strat-20261101-235124).
 *
 * Out of combat: Arcane Intellect / Brilliance, then the armor: Mage Armor in a group (mana
 * regeneration while casting; the healer takes care of the damage), Ice Armor alone. Armors
 * are only put on out of combat, so they are never swapped mid-fight.
 *
 * In combat, in this order:
 *   1. Ice Block when attacked and below ICE_BLOCK_HEALTH_PCT.
 *   2. Ice Barrier when attacked (no mana per absorbed point, unlike Mana Shield). Cold Snap
 *      resets it when attacked, below COLD_SNAP_HEALTH_PCT and nothing absorbs.
 *      Mana Shield is not cast here: EscapeBehavior proposes it when the hits are dangerous.
 *   3. Without the Commander escape module: the old Blink / Frost Nova / run away block.
 *   4. Counterspell (behind the interrupt veto), Polymorph on a second attacker.
 *   5. No area spells (user, 2026-11-02): the Commander area damage module decides when, which
 *      and where (Commander/Behaviors/AreaDamageBehavior.h); without it the mage casts the filler.
 *   6. Burst of other builds (Combustion, Arcane Power, Presence of Mind + Pyroblast).
 *   7. Fire Blast to finish the victim (it dies from it), or while moving above half mana.
 *   8. The filler (ClassKit::Filler: Frostbolt, Fireball for a fire build).
 *   9. Wand when out of mana.
 * Evocation is the Commander's call (ManaBudgetBehavior self restore), not the rotation's.
 * Scorch is not cast: without Improved Scorch it does less per cast than the filler.
 */

#include "Rotations/ClassRotation.h"
#include "PartyBotAI.h"
#include "DamageSpells.h"
#include "Commander/Classes/ClassKit.h"
#include "Player.h"
#include "MotionMaster.h"
#include "Spell.h"

namespace
{
    float const ICE_BLOCK_HEALTH_PCT = 15.0f;
    float const COLD_SNAP_HEALTH_PCT = 40.0f;
    float const LEGACY_MANA_SHIELD_HEALTH_PCT = 50.0f;
    float const MOVING_FIRE_BLAST_MANA_PCT = 50.0f;
    float const WAND_MANA_PCT = 5.0f;

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
                if (bot.CanTryToCastSpell(pTarget, pBuffSpell) && Buff(pTarget, pBuffSpell))
                    return;
            }

            if (SpellEntry const* pArmor = ChooseArmor())
            {
                if (bot.CanTryToCastSpell(me, pArmor) && Buff(me, pArmor))
                    return;
            }

            if (bot.m_isBuffing &&
               (!s.pArcaneIntellect ||
                !me->HasGCD(s.pArcaneIntellect)))
            {
                bot.m_isBuffing = false;
            }

            // Opening on a target before combat starts (pull, assist).
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

            bool const attacked = !me->GetAttackers().empty();

            if (attacked && s.pIceBlock &&
                me->GetHealthPercent() < ICE_BLOCK_HEALTH_PCT &&
                Cast(me, s.pIceBlock))
                return;

            if (attacked && s.pIceBarrier && Cast(me, s.pIceBarrier))
                return;

            if (attacked && s.pColdSnap && s.pIceBarrier &&
                me->GetHealthPercent() < COLD_SNAP_HEALTH_PCT &&
               !me->IsSpellReady(s.pIceBarrier) &&
               !me->HasAuraType(SPELL_AURA_SCHOOL_ABSORB) &&
                Cast(me, s.pColdSnap))
                return;

            if (me->GetMotionMaster()->GetCurrentMovementGeneratorType() == IDLE_MOTION_TYPE
                && me->GetDistance(pVictim) > 30.0f)
            {
                bot.BeginChasing(pVictim);
            }
            else if (bot.GetAttackersInRangeCount(10.0f) && !bot.IsEscapeControlled() && LegacyEscape(pVictim))
                return;

            if (me->GetMotionMaster()->GetCurrentMovementGeneratorType() == DISTANCING_MOTION_TYPE)
                return;

            if (s.pCounterspell &&
                pVictim->IsNonMeleeSpellCasted(false, false, true) && !bot.IsInterruptVetoed(pVictim) &&
                Cast(pVictim, s.pCounterspell))
                return;

            if (s.pPolymorph)
            {
                if (Unit* pTarget = bot.SelectAttackerDifferentFrom(pVictim))
                {
                    if (pTarget->GetHealthPercent() > 20.0f &&
                        bot.CanTryToCastSpell(pTarget, s.pPolymorph) &&
                        bot.CanUseCrowdControl(s.pPolymorph, pTarget) &&
                        bot.DoCastSpell(pTarget, s.pPolymorph) == SPELL_CAST_OK)
                        return;
                }
            }

            if (Burst(pVictim))
                return;

            if (s.pFireBlast &&
               (WouldKill(pVictim, s.pFireBlast) ||
                (me->IsMoving() && me->GetPowerPercent(POWER_MANA) > MOVING_FIRE_BLAST_MANA_PCT)) &&
                Cast(pVictim, s.pFireBlast))
                return;

            if (SpellEntry const* pFiller = bot.Kit().Filler())
                if (Cast(pVictim, pFiller))
                    return;

            if (me->HasSpell(ClassRotations::SPELL_SHOOT_WAND) &&
               !me->IsMoving() &&
               (me->GetPowerPercent(POWER_MANA) < WAND_MANA_PCT) &&
               !me->GetCurrentSpell(CURRENT_AUTOREPEAT_SPELL))
                me->CastSpell(pVictim, ClassRotations::SPELL_SHOOT_WAND, false);
        }

    private:
        bool Cast(Unit* pTarget, SpellEntry const* pSpell)
        {
            return pSpell && m_bot.CanTryToCastSpell(pTarget, pSpell) &&
                   m_bot.DoCastSpell(pTarget, pSpell) == SPELL_CAST_OK;
        }

        bool Buff(Unit* pTarget, SpellEntry const* pSpell)
        {
            if (m_bot.DoCastSpell(pTarget, pSpell) != SPELL_CAST_OK)
                return false;
            m_bot.m_isBuffing = true;
            m_bot.me->ClearTarget();
            return true;
        }

        // Mage Armor in a group, Ice Armor (Frost Armor at low level) alone; the other one when
        // only one is known.
        SpellEntry const* ChooseArmor() const
        {
            auto const& s = m_bot.m_spells.mage;
            if (m_bot.me->GetGroup())
                return s.pMageArmor ? s.pMageArmor : s.pIceArmor;
            return s.pIceArmor ? s.pIceArmor : s.pMageArmor;
        }

        bool WouldKill(Unit const* pVictim, SpellEntry const* pSpell) const
        {
            return float(pVictim->GetHealth()) <= DamageSpells::Estimate(m_bot.me, pVictim, pSpell);
        }

        // Melee enemies near and the Commander escape module off: Blink out of roots / slows,
        // Frost Nova, run away; Mana Shield when hurt.
        bool LegacyEscape(Unit* pVictim)
        {
            PartyBotAI& bot = m_bot;
            Player* me = bot.me;
            auto const& s = bot.m_spells.mage;

            if (s.pManaShield &&
                me->GetHealthPercent() < LEGACY_MANA_SHIELD_HEALTH_PCT &&
               !me->HasAuraType(SPELL_AURA_SCHOOL_ABSORB) &&
                Cast(me, s.pManaShield))
                return true;

            if (bot.GetRole() == ROLE_MELEE_DPS ||
                me->GetMotionMaster()->GetCurrentMovementGeneratorType() == DISTANCING_MOTION_TYPE)
                return false;

            if (s.pBlink &&
                (me->HasUnitState(UNIT_STATE_CAN_NOT_MOVE) ||
                 me->HasAuraType(SPELL_AURA_MOD_DECREASE_SPEED)) &&
                bot.CanTryToCastSpell(me, s.pBlink))
            {
                if (me->GetMotionMaster()->GetCurrentMovementGeneratorType())
                    me->GetMotionMaster()->Clear();

                if (bot.DoCastSpell(me, s.pBlink) == SPELL_CAST_OK)
                    return true;
            }

            if (me->HasUnitState(UNIT_STATE_CAN_NOT_MOVE))
                return false;

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
                return true;
            }
            return false;
        }

        // Cooldowns of fire and arcane builds, as in the original PartyBot rotation.
        bool Burst(Unit* pVictim)
        {
            Player* me = m_bot.me;
            auto const& s = m_bot.m_spells.mage;

            if (s.pCombustion && Cast(me, s.pCombustion))
                return true;

            if (s.pPyroblast &&
               ((s.pPresenceOfMind && me->HasAura(s.pPresenceOfMind->Id)) ||
                (!pVictim->IsInCombat() && (pVictim->GetMaxHealth() > me->GetMaxHealth()) && (me->GetDistance(pVictim) > 30.0f))) &&
                Cast(pVictim, s.pPyroblast))
                return true;

            if (s.pArcanePower &&
                (me->GetPowerPercent(POWER_MANA) > 50.0f) &&
                Cast(me, s.pArcanePower))
                return true;

            if (s.pPresenceOfMind &&
                (me->GetPowerPercent(POWER_MANA) > 50.0f) &&
                Cast(me, s.pPresenceOfMind))
                return true;

            return false;
        }
    };
}

std::unique_ptr<ClassRotation> ClassRotations::MakeMage(PartyBotAI& bot)
{
    return std::make_unique<MageRotation>(bot);
}
