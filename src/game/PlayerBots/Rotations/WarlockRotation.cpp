/*
 * Warlock rotation (moved out of PartyBotAI.cpp 2026-11-14, same steps).
 *
 * Out of combat: Detect Invisibility on the party, Demon Armor; with a victim (an opener): the
 * pet on it, then the combat steps; without one: summon the pet.
 *
 * In combat:
 *   1. Death Coil when the victim is in melee reach of the warlock or casting.
 *   2. Shadowburn below 10%, Searing Pain below 20%.
 *   3. Demonic Sacrifice of a living pet.
 *   4. Immolate, Conflagrate, Corruption, Siphon Life (below 80% health), Drain Life (below
 *      30%), Curse of Agony.
 *   5. Walk closer, Shadow Bolt, Life Tap (below 10% mana, above 70% health), wand below 5% mana.
 *
 * The Commander decides: Banish, Fear, Howl of Terror (crowd control), Rain of Fire (area
 * phase), Amplify Curse (BurstBehavior).
 * Still the rotation's own (doc 2026-11-14-hands-cleanup.md 3): Death Coil.
 */

#include "Rotations/ClassRotation.h"
#include "PartyBotAI.h"
#include "Player.h"
#include "Pet.h"

namespace
{
    float const WAND_MANA_PCT = 5.0f;

    class WarlockRotation : public ClassRotation
    {
    public:
        using ClassRotation::ClassRotation;

        void UpdateOutOfCombat() override
        {
            PartyBotAI& bot = m_bot;
            Player* me = bot.me;
            auto const& s = bot.m_spells.warlock;

            if (s.pDetectInvisibility)
                if (Player* pTarget = bot.SelectBuffTarget(s.pDetectInvisibility))
                    if (Buff(pTarget, s.pDetectInvisibility))
                        return;

            if (Buff(me, s.pDemonArmor))
                return;

            EndBuffing(s.pDetectInvisibility);

            Unit* pVictim = me->GetVictim();
            if (!pVictim)
            {
                bot.SummonPetIfNeeded();
                return;
            }
            SendPetAt(pVictim);
            UpdateInCombat();
        }

        void UpdateInCombat() override
        {
            PartyBotAI& bot = m_bot;
            Player* me = bot.me;
            auto const& s = bot.m_spells.warlock;

            Unit* pVictim = me->GetVictim();
            if (!pVictim)
                return;

            if ((pVictim->CanReachWithMeleeAutoAttack(me) || pVictim->IsNonMeleeSpellCasted()) &&
                Cast(pVictim, s.pDeathCoil))
                return;
            if (pVictim->GetHealthPercent() < 10.0f && Cast(pVictim, s.pShadowburn))
                return;
            if (pVictim->GetHealthPercent() < 20.0f && Cast(pVictim, s.pSearingPain))
                return;

            if (Pet* pPet = me->GetPet())
                if (pPet->IsAlive() && Cast(pPet, s.pDemonicSacrifice))
                    return;

            if (Cast(pVictim, s.pImmolate))
                return;
            if (Cast(pVictim, s.pConflagrate))
                return;
            if (Cast(pVictim, s.pCorruption))
                return;
            if (me->GetHealthPercent() < 80.0f && Cast(pVictim, s.pSiphonLife))
                return;
            if (me->GetHealthPercent() < 30.0f && Cast(pVictim, s.pDrainLife))
                return;
            if (Cast(pVictim, s.pCurseofAgony))
                return;

            ChaseWhenOutOfRange(pVictim);

            if (Cast(pVictim, s.pShadowBolt))
                return;
            if (me->GetPowerPercent(POWER_MANA) < 10.0f && me->GetHealthPercent() > 70.0f && Cast(me, s.pLifeTap))
                return;

            ShootWandBelowMana(pVictim, WAND_MANA_PCT);
        }
    };
}

std::unique_ptr<ClassRotation> ClassRotations::MakeWarlock(PartyBotAI& bot)
{
    return std::make_unique<WarlockRotation>(bot);
}
