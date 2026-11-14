/*
 * Hunter rotation (moved out of PartyBotAI.cpp 2026-11-14, same steps).
 *
 * Out of combat: Aspect of the Hawk; with a victim (an opener): Hunter's Mark, the pet on it,
 * then the combat steps; without one: summon the pet.
 *
 * In combat:
 *   1. Walk closer when idle more than 30 yd away.
 *   2. Volley on 3+ enemies around the victim.
 *   3. Auto Shot beyond 8 yd (ammo added when it has none).
 *   4. Concussive Shot on a victim running at the hunter, Aimed Shot, Arcane Shot, Serpent
 *      Sting, Multi-Shot.
 *   5. An attacker within 8 yd: Scare Beast, Disengage, Aspect of the Monkey.
 *   6. In the victim's melee reach: Wing Clip, Mongoose Bite, Raptor Strike; else back to Hawk.
 * While Auto Shot runs (UpdateWhileAutoRepeating): stopped inside 8 yd, else the same steps.
 *
 * The Commander decides: Feign Death and Deterrence (team rescue), Rapid Fire and Bestial
 * Wrath (BurstBehavior), running from melee (EscapeBehavior).
 * Still the rotation's own (doc 2026-11-14-hands-cleanup.md 3): Scare Beast, Disengage, Aspect
 * of the Monkey, Volley (the area phase needs a hunter Filler first).
 */

#include "Rotations/ClassRotation.h"
#include "PartyBotAI.h"
#include "Player.h"

namespace
{
    float const MIN_SHOT_RANGE = 8.0f;

    class HunterRotation : public ClassRotation
    {
    public:
        using ClassRotation::ClassRotation;

        void UpdateOutOfCombat() override
        {
            PartyBotAI& bot = m_bot;
            Player* me = bot.me;
            auto const& s = bot.m_spells.hunter;

            if (Cast(me, s.pAspectOfTheHawk))
                return;

            Unit* pVictim = me->GetVictim();
            if (!pVictim)
            {
                bot.SummonPetIfNeeded();
                return;
            }

            if (Cast(pVictim, s.pHuntersMark))
                return;
            SendPetAt(pVictim);
            UpdateInCombat();
        }

        void UpdateWhileAutoRepeating() override
        {
            Player* me = m_bot.me;
            if (me->GetCombatDistance(me->GetVictim()) < MIN_SHOT_RANGE)
                me->InterruptSpell(CURRENT_AUTOREPEAT_SPELL, true);
            else
                UpdateInCombat();
        }

        void UpdateInCombat() override
        {
            PartyBotAI& bot = m_bot;
            Player* me = bot.me;
            auto const& s = bot.m_spells.hunter;

            Unit* pVictim = me->GetVictim();
            if (!pVictim)
                return;

            ChaseWhenOutOfRange(pVictim);

            if (me->GetEnemyCountInRadiusAround(pVictim, 10.0f) > 2 && Cast(pVictim, s.pVolley))
                return;

            AutoShot(pVictim);

            if (pVictim->IsMoving() && pVictim->GetVictim() == me && Cast(pVictim, s.pConcussiveShot))
                return;
            if (Cast(pVictim, s.pAimedShot))
                return;
            if (Cast(pVictim, s.pArcaneShot))
                return;
            if (Cast(pVictim, s.pSerpentSting))
                return;
            if (Cast(pVictim, s.pMultiShot))
                return;

            if (bot.GetAttackersInRangeCount(MIN_SHOT_RANGE))
            {
                Unit* pAttacker = *me->GetAttackers().begin();
                if (Cast(pAttacker, s.pScareBeast))
                    return;
                if (Cast(pAttacker, s.pDisengage))
                    return;
                if (Cast(me, s.pAspectOfTheMonkey))
                    return;
            }

            if (pVictim->CanReachWithMeleeAutoAttack(me))
            {
                // (no return: melee abilities are on the next swing)
                Cast(pVictim, s.pWingClip);
                Cast(pVictim, s.pMongooseBite);
                Cast(pVictim, s.pRaptorStrike);
            }
            else
                Cast(me, s.pAspectOfTheHawk);
        }

    private:
        void AutoShot(Unit* pVictim)
        {
            Player* me = m_bot.me;
            if (!me->HasSpell(ClassRotations::SPELL_AUTO_SHOT) || me->IsMoving() ||
                me->GetCombatDistance(pVictim) <= MIN_SHOT_RANGE || me->IsNonMeleeSpellCasted())
                return;
            switch (me->CastSpell(pVictim, ClassRotations::SPELL_AUTO_SHOT, false))
            {
                case SPELL_FAILED_NEED_AMMO:
                case SPELL_FAILED_NO_AMMO:
                    m_bot.AddHunterAmmo();
                    break;
                default:
                    break;
            }
        }
    };
}

std::unique_ptr<ClassRotation> ClassRotations::MakeHunter(PartyBotAI& bot)
{
    return std::make_unique<HunterRotation>(bot);
}
