/*
 * Mage rotation (user, 2026-11-01, after strat-20261101-235124).
 *
 * Out of combat: Arcane Intellect / Brilliance, then the armor: Mage Armor in a group (mana
 * regeneration while casting; the healer takes care of the damage), Ice Armor alone. Armors
 * are only put on out of combat, so they are never swapped mid-fight. With nothing else to do,
 * a mana gem when it has none (ConjureManaGem).
 *
 * In combat, in this order:
 *   1. Ice Barrier when attacked (no mana per absorbed point, unlike Mana Shield).
 *      Mana Shield is not cast here: EscapeBehavior proposes it when the hits are dangerous.
 *   2. No area spells (user, 2026-11-02): the Commander area damage module decides when, which
 *      and where (Commander/Behaviors/Combat/AreaDamageBehavior.h).
 *   3. Pyroblast as an opener on a big enemy not yet in combat, far away, or while Presence
 *      of Mind is on.
 *   4. The filler (ClassKit::Filler: Frostbolt, Fireball for a fire build).
 *   5. Wand when out of mana.
 * The Commander decides: Ice Block, and Cold Snap only to reset it (team rescue), Blink / Frost Nova / running from melee
 * (EscapeBehavior), Counterspell (InterruptBehavior), Polymorph (crowd control), Combustion,
 * Arcane Power, Presence of Mind + Pyroblast (BurstBehavior).
 * No Fire Blast (user 2026-11-14, strat-20261114-180150: "while moving" took the GCD just as
 * the area caster needed Frost Nova; how to use it is decided later).
 * Evocation is the Commander's call (ManaBudgetBehavior self restore), not the rotation's.
 * Scorch is not cast: without Improved Scorch it does less per cast than the filler.
 */

#include "Rotations/ClassRotation.h"
#include "Commander/Team.h"
#include "TeamBotAI.h"
#include "Commander/Classes/ClassKit.h"
#include "Player.h"
#include "MotionMaster.h"
#include "Spell.h"

namespace
{
    float const WAND_MANA_PCT = 5.0f;
    float const CONJURE_MIN_MANA_PCT = 50.0f;

    class MageRotation : public ClassRotation
    {
    public:
        using ClassRotation::ClassRotation;

        void UpdateOutOfCombat() override
        {
            TeamBotAI& bot = m_bot;
            Player* me = bot.me;
            auto const& s = bot.m_spells.mage;

            SpellEntry const* pBuffSpell = nullptr;
            if (Player* pTarget = bot.SelectBuffTarget(s.pArcaneIntellect, s.pArcaneBrilliance, pBuffSpell))
            {
                if (Buff(pTarget, pBuffSpell))
                    return;
            }

            if (Buff(me, ChooseArmor()))
                return;

            EndBuffing(s.pArcaneIntellect);

            // Opening on a target before combat starts (pull, assist).
            if (me->GetVictim())
            {
                UpdateInCombat();
                return;
            }

            ConjureManaGem();
        }

        // Nothing else to do (user 2026-11-15): conjure the best mana gem it knows when it has
        // none, while it and the leader stand still out of combat and the mana stays above
        // CONJURE_MIN_MANA_PCT (resting refills the rest). ManaBudgetBehavior uses the gem.
        bool ConjureManaGem()
        {
            TeamBotAI& bot = m_bot;
            Player* me = bot.me;
            SpellEntry const* pConjure = bot.m_spells.mage.pConjureManaGem;
            if (!pConjure || me->IsMoving() || me->IsInCombat())
                return false;
            uint32 gem = 0;
            for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
                if (pConjure->Effect[i] == SPELL_EFFECT_CREATE_ITEM)
                    gem = uint32(pConjure->EffectItemType[i]);
            if (!gem || me->HasItemCount(gem, 1))
                return false;
            if (Player* pLeader = bot.GetPartyLeader())
                if (pLeader != me && (pLeader->IsMoving() || pLeader->IsInCombat()))
                    return false;
            uint32 const maxMana = me->GetMaxPower(POWER_MANA);
            uint32 const cost = uint32(Spell::CalculatePowerCost(pConjure, me));
            if (!maxMana || me->GetPower(POWER_MANA) < cost ||
                float(me->GetPower(POWER_MANA) - cost) * 100.0f / float(maxMana) < CONJURE_MIN_MANA_PCT)
                return false;
            return Cast(me, pConjure);
        }

        void UpdateInCombat() override
        {
            TeamBotAI& bot = m_bot;
            Player* me = bot.me;
            auto const& s = bot.m_spells.mage;

            Unit* pVictim = me->GetVictim();
            if (!pVictim)
                return;

            bool const attacked = !me->GetAttackers().empty();

            if (attacked && s.pIceBarrier && Cast(me, s.pIceBarrier))
                return;

            ChaseWhenOutOfRange(pVictim);

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

            ShootWandBelowMana(pVictim, WAND_MANA_PCT);
        }

    private:
        // Mage Armor in a group, Ice Armor (Frost Armor at low level) alone; the other one when
        // only one is known.
        SpellEntry const* ChooseArmor() const
        {
            auto const& s = m_bot.m_spells.mage;
            if (Commander::Team::IsShared(m_bot.me))
                return s.pMageArmor ? s.pMageArmor : s.pIceArmor;
            return s.pIceArmor ? s.pIceArmor : s.pMageArmor;
        }

    };
}

std::unique_ptr<ClassRotation> ClassRotations::MakeMage(TeamBotAI& bot)
{
    return std::make_unique<MageRotation>(bot);
}
