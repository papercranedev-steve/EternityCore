/*
 * Rogue rotation (moved out of PartyBotAI.cpp 2026-11-14).
 *
 * Out of combat: main and off hand poisons, Stealth (ClassRotation::KeepStealth); with a victim
 * (an opener): the combat steps.
 *
 * In combat:
 *   1. Stealthed: Premeditation, then Garrote on a caster, Ambush or Cheap Shot otherwise.
 *   2. 5 combo points: Slice and Dice when it is down and the victim above 10%, else Eviscerate.
 *      (Until 2026-11-14 a random finisher among Eviscerate, Kidney Shot, Expose Armor and
 *      Rupture: Kidney Shot is the flee interception's (ClassKit::FleeStoppers), Expose Armor
 *      and Rupture did less than Eviscerate for a party that kills fast.)
 *   3. Backstab, Ghostly Strike, Hemorrhage, Sinister Strike.
 *   4. Sprint when the victim is out of reach and the rogue is not rooted.
 *
 * The Commander decides: Vanish, Evasion, Feint (team rescue), Blind and Sap (crowd control;
 * Sap's stealth: Takeover::Stealth), Kick and Gouge (InterruptBehavior), Adrenaline Rush, Cold
 * Blood, Blade Flurry (BurstBehavior), Eviscerate before a target switch (TargetBehavior).
 */

#include "Rotations/ClassRotation.h"
#include "PartyBotAI.h"
#include "Player.h"

namespace
{
    class RogueRotation : public ClassRotation
    {
    public:
        using ClassRotation::ClassRotation;

        void UpdateOutOfCombat() override
        {
            PartyBotAI& bot = m_bot;
            Player* me = bot.me;
            auto const& s = bot.m_spells.rogue;

            if (s.pMainHandPoison && bot.CanTryToCastSpell(me, s.pMainHandPoison) &&
                bot.CastWeaponBuff(s.pMainHandPoison, EQUIPMENT_SLOT_MAINHAND) == SPELL_CAST_OK)
                return;

            if (s.pOffHandPoison && bot.CanTryToCastSpell(me, s.pOffHandPoison) &&
                bot.CastWeaponBuff(s.pOffHandPoison, EQUIPMENT_SLOT_OFFHAND) == SPELL_CAST_OK)
                return;

            if (KeepStealth(s.pStealth))
                return;

            if (me->GetVictim())
                UpdateInCombat();
        }

        void UpdateInCombat() override
        {
            PartyBotAI& bot = m_bot;
            Player* me = bot.me;
            auto const& s = bot.m_spells.rogue;

            Unit* pVictim = me->GetVictim();
            if (!pVictim)
                return;

            if (me->HasAuraType(SPELL_AURA_MOD_STEALTH))
            {
                // (no return: Premeditation is off the global cooldown)
                Cast(pVictim, s.pPremeditation);

                if (pVictim->IsCaster())
                {
                    if (Cast(pVictim, s.pGarrote))
                        return;
                }
                else if (Cast(pVictim, s.pAmbush) || Cast(pVictim, s.pCheapShot))
                    return;
            }

            if (me->GetComboPoints() > 4)
            {
                bool const sliceDown = s.pSliceAndDice && !me->HasAura(s.pSliceAndDice->Id);
                if (sliceDown && pVictim->GetHealthPercent() > 10.0f)
                {
                    if (Cast(pVictim, s.pSliceAndDice))
                        return;
                }
                else if (Cast(pVictim, s.pEviscerate))
                    return;
            }

            if (Cast(pVictim, s.pBackstab))
                return;
            if (Cast(pVictim, s.pGhostlyStrike))
                return;
            if (Cast(pVictim, s.pHemorrhage))
                return;
            if (Cast(pVictim, s.pSinisterStrike))
                return;

            if (!me->HasUnitState(UNIT_STATE_ROOT) && !me->CanReachWithMeleeAutoAttack(pVictim))
                Cast(me, s.pSprint);
        }
    };
}

std::unique_ptr<ClassRotation> ClassRotations::MakeRogue(PartyBotAI& bot)
{
    return std::make_unique<RogueRotation>(bot);
}
