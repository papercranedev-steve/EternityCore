/*
 * Warrior tank rotation (user, 2026-11-02): the class mechanics only, no decisions.
 * Doc: .kiro/docs/2026-11-01-warrior-tank.md. Damage warriors: Rotations/WarriorRotation.cpp.
 *
 * Out of combat: Defensive Stance (the tank's home stance) only. Battle Shout and Bloodrage moved
 * into the fight (user, 2026-11-15): out of combat they were cast after every fight.
 *
 * In combat, in this order:
 *   1. Back to Defensive Stance when out of it (x1.3 threat; Taunt, Revenge and Shield Block need
 *      it). Not while a swap for a Commander cast is waiting (TeamBotAI::DoCastWithStance).
 *   2. Bloodrage below BLOODRAGE_BELOW_RAGE and above BLOODRAGE_MIN_HEALTH_PCT (no GCD).
 *   3. Revenge (5 rage, the most threat per rage).
 *   4. Shield Block: attacked, shield, below SHIELD_BLOCK_HEALTH_PCT (strat-20261101-210522: with
 *      "or 2+ attackers" it took the rage Sunder and Shield Slam needed).
 *   5. Shield Slam.
 *   6. Battle Shout when missing and rage above RAGE_DUMP (spare rage, after the threat skills).
 *   7. Sunder Armor.
 *   8. Rage above RAGE_DUMP: Cleave (2+ enemies next to the victim) or Heroic Strike, on the next swing.
 *
 * Decisions the rotation no longer makes (user, 2026-11-02), and who makes them:
 *   Charge                 the pull module (PullBehavior, ClassKit::OpenerCharge); the old
 *                          rotation charged every victim out of combat, so stood in Battle Stance.
 *   Thunder Clap opener    the threat module's area threat (ThreatBehavior, stance swap costed).
 *   Demoralizing Shout     the threat module's area threat (ClassKit::AreaThreat); kept up for
 *                          its damage reduction by the mitigation module (MitigationBehavior).
 *   Shield Bash,           the interrupt module (InterruptBehavior, ClassKit::Interrupts); the old
 *   Concussion Blow        rotation stunned on any cast, on moving targets, below 50%.
 *   Shield Wall, Last Stand  the team rescue (RescueAssigner / RescueBehavior).
 *   Taunts                 the team rescue (RescueAssigner / RescueBehavior).
 */

#include "Rotations/ClassRotation.h"
#include "TeamBotAI.h"
#include "Player.h"
#include "MotionMaster.h"

namespace
{
    float const SHIELD_BLOCK_HEALTH_PCT = 60.0f;
    uint32 const RAGE_DUMP = 300;               // 30 rage
    float const CLEAVE_RADIUS = 8.0f;
    uint32 const BLOODRAGE_BELOW_RAGE = 200;    // 20 rage
    float const BLOODRAGE_MIN_HEALTH_PCT = 50.0f;

    class WarriorTankRotation : public ClassRotation
    {
    public:
        using ClassRotation::ClassRotation;

        void UpdateOutOfCombat() override
        {
            // No Battle Shout / Bloodrage here (user, 2026-11-15): rage decays out of combat, so every
            // shout that ran out cost a Bloodrage (health and its cooldown) between fights. Both moved
            // into the fight.
            KeepDefensiveStance();
        }

        void UpdateInCombat() override
        {
            TeamBotAI& bot = m_bot;
            Player* me = bot.me;
            auto const& w = bot.m_spells.warrior;

            Unit* pVictim = me->GetVictim();
            if (!pVictim)
                return;

            if (KeepDefensiveStance())
                return;

            bool const shield = bot.IsWearingShield(me);
            uint32 const rage = me->GetPower(POWER_RAGE);

            // Bloodrage first: no global cooldown, so it costs no threat GCD, and the opening is
            // where the tank is short of rage for Revenge / Sunder. Not when low (it costs health).
            if (rage < BLOODRAGE_BELOW_RAGE && me->GetHealthPercent() > BLOODRAGE_MIN_HEALTH_PCT &&
                Cast(me, w.pBloodrage))
                return;

            if (Cast(pVictim, w.pRevenge))
                return;

            if (shield && !me->GetAttackers().empty() &&
                me->GetHealthPercent() < SHIELD_BLOCK_HEALTH_PCT &&
                Cast(me, w.pShieldBlock))
                return;

            if (shield && Cast(pVictim, w.pShieldSlam))
                return;

            // Battle Shout below the threat skills: only with rage to spare (the same line as the
            // Heroic Strike dump), so it never takes the rage or the GCD Sunder needs. Any
            // warrior's shout of this rank counts.
            if (w.pBattleShout && rage > RAGE_DUMP && !me->HasAura(w.pBattleShout->Id) &&
                Cast(me, w.pBattleShout))
                return;

            if (Cast(pVictim, w.pSunderArmor))
                return;

            // (where the tank stands: the Commander tank stance module, TankStanceBehavior)

            // Rage left over: on the next swing (no global cooldown).
            if (rage > RAGE_DUMP)
            {
                SpellEntry const* pDump = (w.pCleave && me->GetEnemyCountInRadiusAround(pVictim, CLEAVE_RADIUS) > 1) ?
                                           w.pCleave : w.pHeroicStrike;
                Cast(pVictim, pDump);
            }
        }

    private:
        // True = the stance spell was cast: it goes off on the next spell update, another cast
        // in this tick would replace it. A stance on cooldown does not stop the rotation.
        bool KeepDefensiveStance()
        {
            TeamBotAI& bot = m_bot;
            SpellEntry const* pStance = bot.m_spells.warrior.pDefensiveStance;
            if (!pStance || bot.me->GetShapeshiftForm() == FORM_DEFENSIVESTANCE || bot.m_stanceSwap.spell)
                return false;
            return Cast(bot.me, pStance);
        }
    };
}

std::unique_ptr<ClassRotation> ClassRotations::MakeWarriorTank(TeamBotAI& bot)
{
    return std::make_unique<WarriorTankRotation>(bot);
}
