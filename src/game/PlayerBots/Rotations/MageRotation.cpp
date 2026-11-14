/*
 * Mage rotation (user, 2026-11-01, after strat-20261101-235124).
 *
 * Out of combat: Arcane Intellect / Brilliance, then the armor: Mage Armor in a group (mana
 * regeneration while casting; the healer takes care of the damage), Ice Armor alone. Armors
 * are only put on out of combat, so they are never swapped mid-fight.
 *
 * In combat, in this order:
 *   1. Ice Barrier when attacked (no mana per absorbed point, unlike Mana Shield). Cold Snap
 *      resets it when attacked, below COLD_SNAP_HEALTH_PCT and nothing absorbs.
 *      Mana Shield is not cast here: EscapeBehavior proposes it when the hits are dangerous.
 *   2. No area spells (user, 2026-11-02): the Commander area damage module decides when, which
 *      and where (Commander/Behaviors/Combat/AreaDamageBehavior.h).
 *   3. Pyroblast as an opener on a big enemy not yet in combat, far away, or while Presence
 *      of Mind is on.
 *   4. The filler (ClassKit::Filler: Frostbolt, Fireball for a fire build).
 *   5. Wand when out of mana.
 * The Commander decides: Ice Block (team rescue), Blink / Frost Nova / running from melee
 * (EscapeBehavior), Counterspell (InterruptBehavior), Polymorph (crowd control), Combustion,
 * Arcane Power, Presence of Mind + Pyroblast (BurstBehavior).
 * No Fire Blast (user 2026-11-14, strat-20261114-180150: "while moving" took the GCD just as
 * the area caster needed Frost Nova; how to use it is decided later).
 * Evocation is the Commander's call (ManaBudgetBehavior self restore), not the rotation's.
 * Scorch is not cast: without Improved Scorch it does less per cast than the filler.
 */

#include "Rotations/ClassRotation.h"
#include "PartyBotAI.h"
#include "Commander/Classes/ClassKit.h"
#include "Player.h"
#include "MotionMaster.h"
#include "Spell.h"

namespace
{
    float const COLD_SNAP_HEALTH_PCT = 40.0f;
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

            if (me->GetMotionMaster()->GetCurrentMovementGeneratorType() == DISTANCING_MOTION_TYPE)
                return;

            // (a Presence of Mind still on: its Pyroblast, whoever cast it)
            if (s.pPyroblast &&
               ((s.pPresenceOfMind && me->HasAura(s.pPresenceOfMind->Id)) ||
                (!pVictim->IsInCombat() && pVictim->GetMaxHealth() > me->GetMaxHealth() &&
                 me->GetDistance(pVictim) > 30.0f)) &&
                Cast(pVictim, s.pPyroblast))
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

    };
}

std::unique_ptr<ClassRotation> ClassRotations::MakeMage(PartyBotAI& bot)
{
    return std::make_unique<MageRotation>(bot);
}
