/*
 * Priest rotation (moved out of PartyBotAI.cpp 2026-11-14, same steps).
 *
 * Out of combat: Fortitude (Inner Focus first when it is Prayer of Fortitude, so it costs no
 * mana), Divine Spirit, Shadow Protection (single or prayer), Inner Fire,
 * heals (healer); with a victim (an opener): the combat steps.
 *
 * In combat:
 *   1. Power Word: Shield on itself in heal danger or hit below 50%, Inner Focus below 50% mana
 *      (both only while the Commander heal module does not decide heals).
 *   2. Healer (or no victim in caster form): shields on allies about to die (same condition),
 *      heals (the Commander heal module while the party fights).
 *   3. Damage: Shadowform, Vampiric Embrace, Mind Blast, Shadow Word: Pain, Devouring Plague,
 *      Mana Burn on a mana user, Mind Flay (nothing within 10 yd or shielded), walk closer,
 *      Smite (caster form), wand below 10% mana.
 *
 * The Commander decides: Fade, Desperate Prayer (team rescue), Shackle Undead and Psychic
 * Scream (crowd control), Silence (InterruptBehavior), Holy Nova (area phase), Power Infusion
 * (BoostBehavior), dispels in combat (DispelBehavior).
 */

#include "Rotations/ClassRotation.h"
#include "Commander/Team.h"
#include "TeamBotAI.h"
#include "Player.h"

namespace
{
    float const WAND_MANA_PCT = 10.0f;

    class PriestRotation : public ClassRotation
    {
    public:
        using ClassRotation::ClassRotation;

        void UpdateOutOfCombat() override
        {
            TeamBotAI& bot = m_bot;
            Player* me = bot.me;
            auto const& s = bot.m_spells.priest;

            if (PartyBuff(s.pPowerWordFortitude, s.pPrayerofFortitude, s.pInnerFocus) ||
                PartyBuff(s.pDivineSpirit, s.pPrayerofSpirit) ||
                PartyBuff(s.pShadowProtection, s.pPrayerofShadowProtection) ||
                Buff(me, s.pInnerFire))
                return;

            EndBuffing(s.pPowerWordFortitude);

            if (bot.m_role == ROLE_HEALER && bot.FindAndHealInjuredAlly())
                return;

            if (me->GetVictim())
                UpdateInCombat();
        }

        void UpdateInCombat() override
        {
            TeamBotAI& bot = m_bot;
            Player* me = bot.me;
            auto const& s = bot.m_spells.priest;
            bool const healPlanned = bot.IsHealingControlled();

            // Until 2026-11-01 the priest shielded itself whenever the spell was ready (more mana
            // went into shields than into heals). While the Commander heal module decides heals,
            // it decides shields and Inner Focus too (HealPlanBehavior).
            if (!healPlanned &&
               (bot.IsInHealDanger(me) || (me->GetHealthPercent() < 50.0f && bot.GetIncomingDps(me) > 0.0f)) &&
                Cast(me, s.pPowerWordShield))
                return;

            // Made a healer in the fight (a spare healer: Commander CrisisAssigner): no holy
            // spell in Shadowform.
            if (bot.GetRole() == ROLE_HEALER && me->GetShapeshiftForm() == FORM_SHADOW &&
                me->HasAuraType(SPELL_AURA_MOD_SHAPESHIFT))
            {
                me->RemoveSpellsCausingAura(SPELL_AURA_MOD_SHAPESHIFT);
                return;
            }

            if (!healPlanned && me->GetPowerPercent(POWER_MANA) < 50.0f && s.pInnerFocus &&
                bot.CanTryToCastSpell(me, s.pInnerFocus))
                bot.DoCastSpell(me, s.pInnerFocus);

            if (bot.GetRole() == ROLE_HEALER || (!me->GetVictim() && me->GetShapeshiftForm() == FORM_NONE))
            {
                if (!healPlanned && s.pPowerWordShield)
                    if (Player* pTarget = bot.SelectShieldTarget())
                        if (Cast(pTarget, s.pPowerWordShield))
                            return;

                // Heal or HoT, chosen by ChooseHeal (HealSpells.h).
                if (bot.FindAndHealInjuredAlly(80.0f, 90.0f))
                    return;

                if (bot.GetRole() == ROLE_HEALER)
                    bot.FindAndPreHealTarget();
            }
            // Alone (a team of one, Commander/Team.h): nobody else heals it, as the paladin and
            // shaman rotations heal themselves when low.
            else if (!healPlanned && !Commander::Team::IsShared(me) && me->GetHealthPercent() < SOLO_SELF_HEAL_PCT &&
                     bot.HealInjuredTarget(me))
                return;
            else if (Unit* pVictim = me->GetVictim())
                Damage(pVictim);
        }

        static constexpr float SOLO_SELF_HEAL_PCT = 40.0f;

    private:
        // pFreeCast (Inner Focus): cast first when the group version is chosen, so the prayer
        // costs no mana (user, 2026-11-15). The prayer follows on the next update.
        bool PartyBuff(SpellEntry const* pSingle, SpellEntry const* pGroup, SpellEntry const* pFreeCast = nullptr)
        {
            SpellEntry const* pBuffSpell = nullptr;
            Player* pTarget = m_bot.SelectBuffTarget(pSingle, pGroup, pBuffSpell);
            if (!pTarget)
                return false;

            Player* me = m_bot.me;
            if (pFreeCast && pBuffSpell == pGroup && !me->HasAura(pFreeCast->Id) && Cast(me, pFreeCast))
                return true;

            return Buff(pTarget, pBuffSpell);
        }

        void Damage(Unit* pVictim)
        {
            TeamBotAI& bot = m_bot;
            Player* me = bot.me;
            auto const& s = bot.m_spells.priest;

            if (Cast(me, s.pShadowform))
                return;
            if (Cast(pVictim, s.pVampiricEmbrace))
                return;
            if (Cast(pVictim, s.pMindBlast))
                return;
            if (Cast(pVictim, s.pShadowWordPain))
                return;
            if (Cast(pVictim, s.pDevouringPlague))
                return;
            if (pVictim->GetPowerType() == POWER_MANA && Cast(pVictim, s.pManaBurn))
                return;
            if ((!bot.GetAttackersInRangeCount(10.0f) || me->HasAuraType(SPELL_AURA_SCHOOL_ABSORB)) &&
                Cast(pVictim, s.pMindFlay))
                return;

            ChaseWhenOutOfRange(pVictim);

            if (me->GetShapeshiftForm() == FORM_NONE && Cast(pVictim, s.pSmite))
                return;

            ShootWandBelowMana(pVictim, WAND_MANA_PCT);
        }
    };
}

std::unique_ptr<ClassRotation> ClassRotations::MakePriest(TeamBotAI& bot)
{
    return std::make_unique<PriestRotation>(bot);
}
