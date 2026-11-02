/*
 * Warrior tank rotation (user, 2026-11-02): the class mechanics only, no decisions.
 * Doc: .kiro/docs/2026-11-01-warrior-tank.md. Damage warriors still run
 * PartyBotAI::UpdateInCombatAI_Warrior.
 *
 * Out of combat: Defensive Stance (the tank's home stance), Battle Shout (Bloodrage for the rage
 * when it has none).
 *
 * In combat, in this order:
 *   1. Back to Defensive Stance when out of it (x1.3 threat; Taunt, Revenge and Shield Block need
 *      it). Not while a swap for a Commander cast is waiting (PartyBotAI::DoCastWithStance).
 *   2. Shield Bash on a cast, behind the interrupt veto (as the other rotations' reflex interrupts).
 *   3. Revenge (5 rage, the most threat per rage).
 *   4. Shield Block: attacked, shield, below SHIELD_BLOCK_HEALTH_PCT (strat-20261101-210522: with
 *      "or 2+ attackers" it took the rage Sunder and Shield Slam needed).
 *   5. Shield Slam, 6. Sunder Armor.
 *   7. Rage above RAGE_DUMP: Cleave (2+ enemies next to the victim) or Heroic Strike, on the next swing.
 *
 * Decisions the rotation no longer makes (user, 2026-11-02), and who makes them:
 *   Charge                 the pull module (PullBehavior, ClassKit::OpenerCharge); the old
 *                          rotation charged every victim out of combat, so stood in Battle Stance.
 *   Thunder Clap opener    the threat module's area threat (ThreatBehavior, stance swap costed).
 *   Demoralizing Shout     the threat module's area threat (ClassKit::AreaThreat).
 *   Concussion Blow        the interrupt module (ClassKit::Interrupts); the old rotation stunned
 *                          on any cast without the interrupt veto, on moving targets, below 50%.
 *   Shield Wall, Last Stand  nobody for now: a Commander survival decision is planned (user).
 *   Taunts                 the shared tank step in PartyBotAI::UpdateInCombatAI while the threat
 *                          module is off, else the threat module.
 */

#include "Rotations/ClassRotation.h"
#include "PartyBotAI.h"
#include "Player.h"
#include "MotionMaster.h"

namespace
{
    float const SHIELD_BLOCK_HEALTH_PCT = 60.0f;
    uint32 const RAGE_DUMP = 300;               // 30 rage
    float const CLEAVE_RADIUS = 8.0f;

    class WarriorTankRotation : public ClassRotation
    {
    public:
        using ClassRotation::ClassRotation;

        void UpdateOutOfCombat() override
        {
            PartyBotAI& bot = m_bot;
            Player* me = bot.me;
            auto const& w = bot.m_spells.warrior;

            if (KeepDefensiveStance())
                return;

            if (w.pBattleShout && !me->HasAura(w.pBattleShout->Id))
            {
                if (Cast(me, w.pBattleShout))
                    return;
                if (w.pBloodrage && me->GetPower(POWER_RAGE) < 100 && Cast(me, w.pBloodrage))
                    return;
            }
        }

        void UpdateInCombat() override
        {
            PartyBotAI& bot = m_bot;
            Player* me = bot.me;
            auto const& w = bot.m_spells.warrior;

            Unit* pVictim = me->GetVictim();
            if (!pVictim)
                return;

            if (KeepDefensiveStance())
                return;

            bool const shield = bot.IsWearingShield(me);

            if (shield && pVictim->IsNonMeleeSpellCasted(false, false, true) && !bot.IsInterruptVetoed(pVictim) &&
                Cast(pVictim, w.pShieldBash))
                return;

            if (Cast(pVictim, w.pRevenge))
                return;

            if (shield && !me->GetAttackers().empty() &&
                me->GetHealthPercent() < SHIELD_BLOCK_HEALTH_PCT &&
                Cast(me, w.pShieldBlock))
                return;

            if (shield && Cast(pVictim, w.pShieldSlam))
                return;

            if (Cast(pVictim, w.pSunderArmor))
                return;

            if (me->GetMotionMaster()->GetCurrentMovementGeneratorType() == IDLE_MOTION_TYPE &&
               !me->CanReachWithMeleeAutoAttack(pVictim))
                bot.BeginChasing(pVictim);

            // Rage left over: on the next swing (no global cooldown).
            if (me->GetPower(POWER_RAGE) > RAGE_DUMP)
            {
                SpellEntry const* pDump = (w.pCleave && me->GetEnemyCountInRadiusAround(pVictim, CLEAVE_RADIUS) > 1) ?
                                           w.pCleave : w.pHeroicStrike;
                Cast(pVictim, pDump);
            }
        }

    private:
        bool Cast(Unit* pTarget, SpellEntry const* pSpell)
        {
            return pSpell && m_bot.CanTryToCastSpell(pTarget, pSpell) &&
                   m_bot.DoCastSpell(pTarget, pSpell) == SPELL_CAST_OK;
        }

        // True = the stance spell was cast: it goes off on the next spell update, another cast
        // in this tick would replace it. A stance on cooldown does not stop the rotation.
        bool KeepDefensiveStance()
        {
            PartyBotAI& bot = m_bot;
            SpellEntry const* pStance = bot.m_spells.warrior.pDefensiveStance;
            if (!pStance || bot.me->GetShapeshiftForm() == FORM_DEFENSIVESTANCE || bot.m_stanceSwap.spell)
                return false;
            return Cast(bot.me, pStance);
        }
    };
}

std::unique_ptr<ClassRotation> ClassRotations::MakeWarriorTank(PartyBotAI& bot)
{
    return std::make_unique<WarriorTankRotation>(bot);
}
