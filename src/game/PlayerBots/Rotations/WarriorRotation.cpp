/*
 * Damage warrior rotation (moved out of PartyBotAI.cpp 2026-11-14). Tanks:
 * Rotations/WarriorTankRotation.cpp.
 *
 * Out of combat: Battle Stance, Charge an opener victim. Battle Shout and Bloodrage moved into
 * the fight (user, 2026-11-15): out of combat they were cast after every fight.
 *
 * In combat:
 *   0. Bloodrage below 20 rage and above 50% health (no GCD).
 *   1. Execute below 20%, Overpower, Concussion Blow (victim moving or the warrior below 50%).
 *   2. Defensive Stance with a shield: Shield Block when attacked, Shield Slam.
 *   3. Hamstring on a moving victim not already slowed, Rend, Mortal Strike, Bloodthirst.
 *   3b. Battle Shout when missing and rage above 30.
 *   4. Defensive Stance below 20% health, Berserker Stance otherwise.
 *   5. Intercept, Whirlwind, Disarm a melee class.
 *   6. Rage above 30: Cleave (2+ enemies next to the victim) or Heroic Strike.
 *   No victim: Battle Shout when missing.
 *
 * The Commander decides: Pummel, Shield Bash, Concussion Blow on a cast (InterruptBehavior; the
 * rotation's "victim casting" condition went 2026-11-14), Last Stand, Shield Wall (team rescue),
 * Intimidating Shout (crowd control), Retaliation, Sweeping Strikes, Recklessness, Death Wish
 * (BurstBehavior).
 * Still the rotation's own (doc 2026-11-14-hands-cleanup.md 3): Concussion Blow, Disarm.
 */

#include "Rotations/ClassRotation.h"
#include "PartyBotAI.h"
#include "Player.h"

namespace
{
    uint32 const RAGE_DUMP = 300;               // 30 rage
    float const CLEAVE_RADIUS = 8.0f;
    uint32 const BLOODRAGE_BELOW_RAGE = 200;    // 20 rage
    float const BLOODRAGE_MIN_HEALTH_PCT = 50.0f;

    class WarriorRotation : public ClassRotation
    {
    public:
        using ClassRotation::ClassRotation;

        void UpdateOutOfCombat() override
        {
            PartyBotAI& bot = m_bot;
            Player* me = bot.me;
            auto const& w = bot.m_spells.warrior;

            if (Cast(me, w.pBattleStance))
                return;

            // No Battle Shout / Bloodrage here (user, 2026-11-15): rage decays out of combat, so every
            // shout that ran out cost a Bloodrage between fights. Both moved into the fight.

            if (Unit* pVictim = me->GetVictim())
                Cast(pVictim, w.pCharge);
        }

        void UpdateInCombat() override
        {
            PartyBotAI& bot = m_bot;
            Player* me = bot.me;
            auto const& w = bot.m_spells.warrior;

            bool const shoutMissing = w.pBattleShout && !me->HasAura(w.pBattleShout->Id);

            Unit* pVictim = me->GetVictim();
            if (!pVictim)
            {
                if (shoutMissing)
                    Cast(me, w.pBattleShout);
                return;
            }

            uint32 const rage = me->GetPower(POWER_RAGE);

            // Bloodrage: no global cooldown, rage for the opening. Not when low (it costs health).
            if (rage < BLOODRAGE_BELOW_RAGE && me->GetHealthPercent() > BLOODRAGE_MIN_HEALTH_PCT &&
                Cast(me, w.pBloodrage))
                return;

            if (pVictim->GetHealthPercent() < 20.0f && Cast(pVictim, w.pExecute))
                return;
            if (Cast(pVictim, w.pOverpower))
                return;
            if ((pVictim->IsMoving() || me->GetHealthPercent() < 50.0f) && Cast(pVictim, w.pConcussionBlow))
                return;

            if (me->GetShapeshiftForm() == FORM_DEFENSIVESTANCE && bot.IsWearingShield(me))
            {
                if (!me->GetAttackers().empty() && Cast(me, w.pShieldBlock))
                    return;
                if (Cast(pVictim, w.pShieldSlam))
                    return;
            }

            if (pVictim->IsMoving() && !pVictim->HasUnitState(UNIT_STATE_ROOT) &&
               !pVictim->HasAuraType(SPELL_AURA_MOD_DECREASE_SPEED) && Cast(pVictim, w.pHamstring))
                return;
            if (Cast(pVictim, w.pRend))
                return;
            if (Cast(pVictim, w.pMortalStrike))
                return;
            if (Cast(pVictim, w.pBloodthirst))
                return;

            // Battle Shout after the main strikes, only with rage to spare (the Heroic Strike line),
            // so it never takes the rage Mortal Strike / Bloodthirst need.
            if (shoutMissing && rage > RAGE_DUMP && Cast(me, w.pBattleShout))
                return;

            // (no return: the stance goes off on the next spell update)
            Cast(me, me->GetHealthPercent() < 20.0f ? w.pDefensiveStance : w.pBerserkerStance);

            if (Cast(pVictim, w.pIntercept))
                return;
            if (Cast(me, w.pWhirlwind))
                return;
            if (bot.IsMeleeWeaponClass(pVictim->GetClass()) && Cast(pVictim, w.pDisarm))
                return;

            // (the chase: UpdateAI restarts an idle one before the rotation, or the Commander
            // melee stance module chases)

            if (rage > RAGE_DUMP)
            {
                SpellEntry const* pDump = (w.pCleave && me->GetEnemyCountInRadiusAround(pVictim, CLEAVE_RADIUS) > 1) ?
                                           w.pCleave : w.pHeroicStrike;
                Cast(pVictim, pDump);
            }
        }
    };
}

std::unique_ptr<ClassRotation> ClassRotations::MakeWarrior(PartyBotAI& bot)
{
    return std::make_unique<WarriorRotation>(bot);
}
