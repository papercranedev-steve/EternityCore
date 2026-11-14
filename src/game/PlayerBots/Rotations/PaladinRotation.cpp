/*
 * Paladin rotation (moved out of PartyBotAI.cpp 2026-11-14, same steps).
 *
 * Out of combat: aura, Righteous Fury (tank), blessing on the party, heals (healer).
 *
 * In combat:
 *   1. Blessing of Sacrifice on an ally below 70% while the paladin is above 80%.
 *   2. Attacked: Holy Shield; Turn Evil on a second undead attacker (not a tank).
 *   3. Healer: heals (the Commander heal module while the party fights).
 *      Others: seal, Judgement, Hammer of Justice below 20% health, Hammer of Wrath on a victim
 *      below 20%, Consecration (not a tank), Holy Shock (+ Divine Favor), Exorcism on undead,
 *      Holy Wrath on undead / demons.
 *   4. Blessing of Freedom when rooted or slowed; a self heal below 30% (not a healer).
 *
 * The Commander decides: Divine Shield, Blessing of Protection, Lay on Hands (team rescue),
 * Hammer of Justice on a cast (InterruptBehavior), Consecration for a tank (ThreatBehavior,
 * ClassKit::AreaThreat; the rotation cast it on "3 attackers" too until 2026-11-14), dispels
 * in combat (DispelBehavior), Divine Favor before a healer's heal (heal boosts).
 * Still the rotation's own (no Commander module yet, doc 2026-11-14-hands-cleanup.md 3):
 * Turn Evil, Blessing of Sacrifice, Hammer of Justice at low health.
 */

#include "Rotations/ClassRotation.h"
#include "PartyBotAI.h"
#include "Player.h"

namespace
{
    class PaladinRotation : public ClassRotation
    {
    public:
        using ClassRotation::ClassRotation;

        void UpdateOutOfCombat() override
        {
            PartyBotAI& bot = m_bot;
            Player* me = bot.me;
            auto const& s = bot.m_spells.paladin;

            if (Cast(me, s.pAura))
                return;

            if (bot.m_role == ROLE_TANK && Cast(me, s.pRighteousFury))
                return;

            if (s.pBlessingBuff)
                if (Player* pTarget = bot.SelectBuffTarget(s.pBlessingBuff))
                    if (Buff(pTarget, s.pBlessingBuff))
                        return;

            EndBuffing(s.pBlessingBuff);

            if (bot.m_role == ROLE_HEALER && bot.FindAndHealInjuredAlly())
                return;
        }

        void UpdateInCombat() override
        {
            PartyBotAI& bot = m_bot;
            Player* me = bot.me;
            auto const& s = bot.m_spells.paladin;

            if (Unit* pFriend = me->FindLowestHpFriendlyUnit(30.0f, 70, true, me))
                if (s.pBlessingOfSacrifice && me->GetHealthPercent() > 80.0f && Cast(pFriend, s.pBlessingOfSacrifice))
                    return;

            if (!me->GetAttackers().empty())
            {
                if (Cast(me, s.pHolyShield))
                    return;

                if (s.pTurnEvil && bot.m_role != ROLE_TANK)
                {
                    Unit* pAttacker = bot.SelectAttackerDifferentFrom(me->GetVictim());
                    if (pAttacker && pAttacker->GetCreatureType() == CREATURE_TYPE_UNDEAD && Cast(pAttacker, s.pTurnEvil))
                        return;
                }
            }

            if (bot.GetRole() == ROLE_HEALER)
            {
                if (bot.FindAndHealInjuredAlly(80.0f, 90.0f))
                    return;
                if (bot.FindAndPreHealTarget())
                    return;
            }
            else if (Damage())
                return;

            if ((me->HasUnitState(UNIT_STATE_ROOT) || me->HasAuraType(SPELL_AURA_MOD_DECREASE_SPEED)) &&
                Cast(me, s.pBlessingOfFreedom))
                return;

            if (bot.GetRole() != ROLE_HEALER && me->GetHealthPercent() < 30.0f)
                bot.HealInjuredTarget(me);
        }

    private:
        // True = cast.
        bool Damage()
        {
            PartyBotAI& bot = m_bot;
            Player* me = bot.me;
            auto const& s = bot.m_spells.paladin;

            bool const hasSeal = s.pSeal && me->HasAura(s.pSeal->Id);
            if (!hasSeal && s.pSeal && bot.CanTryToCastSpell(me, s.pSeal))
                me->CastSpell(me, s.pSeal, false);

            Unit* pVictim = me->GetVictim();
            if (!pVictim)
                return false;

            if (hasSeal && me->GetPowerPercent(POWER_MANA) > 30.0f && Cast(pVictim, s.pJudgement))
                return true;

            if (me->GetHealthPercent() < 20.0f && !me->GetAttackers().empty() && Cast(pVictim, s.pHammerOfJustice))
                return true;

            if (pVictim->GetHealthPercent() < 20.0f && Cast(pVictim, s.pHammerOfWrath))
                return true;

            if (bot.m_role != ROLE_TANK && bot.GetAttackersInRangeCount(10.0f) > 2 && Cast(me, s.pConsecration))
                return true;

            if (s.pHolyShock && bot.CanTryToCastSpell(pVictim, s.pHolyShock))
            {
                if (s.pDivineFavor && bot.CanTryToCastSpell(me, s.pDivineFavor))
                    bot.DoCastSpell(me, s.pDivineFavor);
                if (bot.DoCastSpell(pVictim, s.pHolyShock) == SPELL_CAST_OK)
                    return true;
            }

            bool const undead = pVictim->IsCreature() && pVictim->GetCreatureType() == CREATURE_TYPE_UNDEAD;
            bool const demon = pVictim->IsCreature() && pVictim->GetCreatureType() == CREATURE_TYPE_DEMON;

            if (undead && Cast(pVictim, s.pExorcism))
                return true;

            // (fewer than 3 attackers: too much pushback)
            if ((undead || demon) && me->GetAttackers().size() < 3 && Cast(pVictim, s.pHolyWrath))
                return true;

            // (the chase: UpdateAI restarts an idle one before the rotation, or the Commander
            // melee stance module chases)
            return false;
        }
    };
}

std::unique_ptr<ClassRotation> ClassRotations::MakePaladin(PartyBotAI& bot)
{
    return std::make_unique<PaladinRotation>(bot);
}
