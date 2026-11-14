/*
 * Damage warrior rotation (moved out of PartyBotAI.cpp 2026-11-14). Tanks:
 * Rotations/WarriorTankRotation.cpp.
 *
 * Out of combat: Battle Stance, Battle Shout (Bloodrage for the rage), Charge an opener victim.
 *
 * In combat:
 *   1. Execute below 20%, Overpower, Concussion Blow (victim moving or the warrior below 50%).
 *   2. Defensive Stance with a shield: Shield Block when attacked, Shield Slam.
 *   3. Hamstring on a moving victim not already slowed, Rend, Mortal Strike, Bloodthirst.
 *   4. Defensive Stance below 20% health, Berserker Stance otherwise.
 *   5. Intercept, Whirlwind, Disarm a melee class.
 *   6. Rage above 30: Cleave (2+ enemies next to the victim) or Heroic Strike.
 *   No victim: Battle Shout.
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

            if (w.pBattleShout && !me->HasAura(w.pBattleShout->Id) && !Cast(me, w.pBattleShout) &&
                me->GetPower(POWER_RAGE) < 100)
                Cast(me, w.pBloodrage);

            if (Unit* pVictim = me->GetVictim())
                Cast(pVictim, w.pCharge);
        }

        void UpdateInCombat() override
        {
            PartyBotAI& bot = m_bot;
            Player* me = bot.me;
            auto const& w = bot.m_spells.warrior;

            Unit* pVictim = me->GetVictim();
            if (!pVictim)
            {
                Cast(me, w.pBattleShout);
                return;
            }

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

            if (me->GetPower(POWER_RAGE) > RAGE_DUMP)
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
