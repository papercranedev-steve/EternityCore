/*
 * Druid rotation (moved out of PartyBotAI.cpp 2026-11-14). One class, three forms: the role
 * picks the form (cat: melee damage, bear: tank or melee without cat, moonkin: ranged damage),
 * the form picks the steps.
 *
 * Out of combat: healer leaves its form; Mark / Gift of the Wild, Thorns, Nature's Grasp;
 * caster form: into the combat form, heals above 80% mana; cat: Prowl (ClassRotation::
 * KeepStealth); with a victim (an opener): the combat steps.
 *
 * In combat:
 *   Caster form: heals (the Commander heal module while the party fights), then into the
 *   combat form. Rooted in a form (not a tank in reach): leave the form.
 *   Cat: stealthed Pounce / Ravage / Tiger's Fury; 5 combo points Ferocious Bite, Rip; out of
 *   reach Faerie Fire (Feral), Dash on a moving victim; Shred, Rake, Claw.
 *   Bear: Faerie Fire (Feral), Demoralizing Roar and Swipe (not a tank: on 2+ attackers or 80
 *   rage), Maul.
 *   Caster / moonkin damage: walk closer, Insect Swarm, Moonfire, Starfire (victim above 50%),
 *   Wrath.
 *
 * The Commander decides: Barkskin, Frenzied Regeneration (team rescue), Hibernate (crowd
 * control), Innervate (BoostBehavior), Nature's Swiftness (heal boosts), Hurricane (area
 * phase), Entangling Roots and running from melee (EscapeBehavior), dispels in combat.
 * Taken out 2026-11-14: Bash and Feral Charge on cooldown (InterruptBehavior assigns them,
 * ClassKit::Interrupts); a tank's Swipe and Demoralizing Roar (ThreatBehavior, ClassKit::
 * AreaThreat, as for the warrior tank); Faerie Fire only on a rogue victim (a PvP leftover).
 */

#include "Rotations/ClassRotation.h"
#include "PartyBotAI.h"
#include "Player.h"
#include "MotionMaster.h"

namespace
{
    class DruidRotation : public ClassRotation
    {
    public:
        using ClassRotation::ClassRotation;

        void UpdateOutOfCombat() override
        {
            PartyBotAI& bot = m_bot;
            Player* me = bot.me;
            auto const& s = bot.m_spells.druid;

            // Leave a combat form when the role changed to healer.
            if (bot.GetRole() == ROLE_HEALER && me->GetShapeshiftForm() != FORM_NONE &&
                me->HasAuraType(SPELL_AURA_MOD_SHAPESHIFT))
            {
                me->RemoveSpellsCausingAura(SPELL_AURA_MOD_SHAPESHIFT);
                return;
            }

            SpellEntry const* pBuffSpell = nullptr;
            if (Player* pTarget = bot.SelectBuffTarget(s.pMarkoftheWild, s.pGiftoftheWild, pBuffSpell))
                if (Buff(pTarget, pBuffSpell))
                    return;

            if (s.pThorns)
                if (Player* pTarget = bot.SelectBuffTarget(s.pThorns))
                    if (Buff(pTarget, s.pThorns))
                        return;

            if (Cast(me, s.pNaturesGrasp))
                return;

            EndBuffing(s.pMarkoftheWild);

            if (me->GetShapeshiftForm() == FORM_NONE)
            {
                if (EnterCombatForm())
                    return;
                if (me->GetPowerPercent(POWER_MANA) > 80.0f && bot.FindAndHealInjuredAlly())
                    return;
            }
            else if (me->GetShapeshiftForm() == FORM_CAT)
            {
                if (KeepStealth(s.pProwl))
                    return;
            }

            if (me->GetVictim())
                UpdateInCombat();
        }

        void UpdateInCombat() override
        {
            PartyBotAI& bot = m_bot;
            Player* me = bot.me;
            ShapeshiftForm const form = me->GetShapeshiftForm();

            if (form == FORM_NONE)
            {
                // HoT or direct heal, chosen by ChooseHeal (HealSpells.h).
                if (bot.FindAndHealInjuredAlly(80.0f, 90.0f))
                    return;
                if (bot.GetRole() == ROLE_HEALER && bot.FindAndPreHealTarget())
                    return;
                if (EnterCombatForm())
                    return;
            }

            Unit* pVictim = me->GetVictim();
            if (!pVictim)
                return;

            if (form != FORM_NONE && me->HasUnitState(UNIT_STATE_ROOT) &&
                me->HasAuraType(SPELL_AURA_MOD_SHAPESHIFT) &&
               (bot.m_role != ROLE_TANK || !me->CanReachWithMeleeAutoAttack(pVictim)))
                me->RemoveSpellsCausingAura(SPELL_AURA_MOD_SHAPESHIFT);

            if (bot.GetRole() == ROLE_HEALER)
                return;

            switch (form)
            {
                case FORM_CAT:
                    Cat(pVictim);
                    break;
                case FORM_BEAR:
                case FORM_DIREBEAR:
                    Bear(pVictim);
                    break;
                case FORM_NONE:
                case FORM_MOONKIN:
                    Caster(pVictim);
                    break;
                default:
                    break;
            }
        }

    private:
        // Cat for melee damage, bear for a tank (or melee without cat), moonkin for ranged damage.
        bool EnterCombatForm()
        {
            PartyBotAI& bot = m_bot;
            auto const& s = bot.m_spells.druid;
            CombatBotRoles const role = bot.GetRole();

            if (role == ROLE_MELEE_DPS && Cast(bot.me, s.pCatForm))
                return true;
            if ((bot.m_role == ROLE_TANK || role == ROLE_MELEE_DPS) && Cast(bot.me, s.pBearForm))
                return true;
            if (role == ROLE_RANGE_DPS && Cast(bot.me, s.pMoonkinForm))
                return true;
            return false;
        }

        void MeleeChaseDistance()
        {
            if (m_bot.me->HasDistanceCasterMovement())
                m_bot.me->SetCasterChaseDistance(0.0f);
            // (the chase: UpdateAI restarts an idle one before the rotation, or the Commander
            // tank / melee stance module chases)
        }

        void Cat(Unit* pVictim)
        {
            Player* me = m_bot.me;
            auto const& s = m_bot.m_spells.druid;
            MeleeChaseDistance();

            if (me->HasAuraType(SPELL_AURA_MOD_STEALTH))
            {
                if (!Cast(pVictim, s.pPounce) && !Cast(pVictim, s.pRavage))
                    Cast(me, s.pTigersFury);
                return;
            }

            if (me->GetComboPoints() > 4 && (Cast(pVictim, s.pFerociousBite) || Cast(pVictim, s.pRip)))
                return;

            if (!me->CanReachWithMeleeAutoAttack(pVictim))
            {
                if (Cast(pVictim, s.pFaerieFireFeral))
                    return;
                if (pVictim->IsMoving() && Cast(me, s.pDash))
                    return;
            }

            if (Cast(pVictim, s.pShred) || Cast(pVictim, s.pRake) || Cast(pVictim, s.pClaw))
                return;
        }

        void Bear(Unit* pVictim)
        {
            PartyBotAI& bot = m_bot;
            auto const& s = bot.m_spells.druid;
            MeleeChaseDistance();

            if (Cast(pVictim, s.pFaerieFireFeral))
                return;

            if (bot.m_role != ROLE_TANK &&
               (bot.me->GetPower(POWER_RAGE) > 800 || bot.GetAttackersInRangeCount(10.0f) > 1) &&
               (Cast(pVictim, s.pDemoralizingRoar) || Cast(pVictim, s.pSwipe)))
                return;

            Cast(pVictim, s.pMaul);
        }

        void Caster(Unit* pVictim)
        {
            Player* me = m_bot.me;
            auto const& s = m_bot.m_spells.druid;

            ChaseWhenOutOfRange(pVictim);

            if (Cast(pVictim, s.pInsectSwarm))
                return;

            if (me->GetMotionMaster()->GetCurrentMovementGeneratorType() == DISTANCING_MOTION_TYPE)
                return;

            if (Cast(pVictim, s.pMoonfire))
                return;
            if (pVictim->GetHealthPercent() > 50.0f && Cast(pVictim, s.pStarfire))
                return;
            Cast(pVictim, s.pWrath);
        }
    };
}

std::unique_ptr<ClassRotation> ClassRotations::MakeDruid(PartyBotAI& bot)
{
    return std::make_unique<DruidRotation>(bot);
}
