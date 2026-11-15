/*
 * Hunter rotation (moved out of PartyBotAI.cpp 2026-11-14; shots reworked 2026-11-15 for the
 * beast mastery spec, user).
 *
 * Out of combat: Aspect of the Hawk; with a victim (an opener): Hunter's Mark, the pet on it,
 * then the combat steps. The pet: PartyBotAI::UpdateAI out of combat (SummonPetIfNeeded).
 *
 * In combat:
 *   0. Melee auto attack on (user 2026-11-15: a hunter is ranged and melee; without it the boss
 *      stood in its face for 12 s, the queued Raptor Strike never swung, strat-20261115-181613).
 *   1. Walk closer when idle more than 30 yd away.
 *   2. Auto Shot beyond 8 yd (ammo added when it has none).
 *   3. Concussive Shot on a victim running at the hunter.
 *   4. Aimed Shot right after an Auto Shot went off (AimedShotFits): Auto Shot's timer runs on
 *      during the 3 s cast and the next shot comes at the earliest 0.5 s after it
 *      (Unit::_UpdateAutoRepeatSpell), so started just after a shot it costs about one second
 *      of shooting, started just before one a whole shot. Highest rank; rank 1 below
 *      LOW_MANA_PCT (rank 6: 310 mana, +600 damage; rank 1: 75 mana, +70).
 *   5. Multi-Shot rank 1 only (pMultiShotRank1).
 *   6. Serpent Sting above LOW_MANA_PCT (whether it pays before the enemy dies:
 *      Constraints::WastedCast in the party cast checks).
 *   7. In the victim's melee reach: Mongoose Bite, Raptor Strike.
 * While Auto Shot runs (UpdateWhileAutoRepeating): stopped inside 8 yd, else the same steps.
 *
 * Not cast: Arcane Shot (190 mana for 183 damage, the worst shot for mana), Volley, Scare
 * Beast, Disengage (1.12: threat only, in melee), Aspect of the Monkey (the aspect stays Hawk).
 * The Commander decides: Feign Death and Deterrence (team rescue), Rapid Fire and Bestial
 * Wrath (BurstBehavior), Wing Clip / Intimidation and the step out of the dead zone
 * (KiteBehavior), running to the tank (EscapeBehavior), where it stands (RangedStanceBehavior,
 * outside the dead zone: ClassKit::MinAttackRange).
 */

#include "Rotations/ClassRotation.h"
#include "PartyBotAI.h"
#include "Player.h"
#include "Spell.h"
#include <algorithm>

namespace
{
    float const MIN_SHOT_RANGE = 8.0f;
    // Below this mana percent: Aimed Shot rank 1, no Serpent Sting.
    float const LOW_MANA_PCT = 40.0f;
    // Aimed Shot starts within this long after an Auto Shot went off (bot ticks are 200 ms).
    uint32 const AIMED_AFTER_SHOT_MS = 600;
    // Auto Shot's wind-up after a cast (Unit::_UpdateAutoRepeatSpell).
    uint32 const AUTO_SHOT_WINDUP_MS = 500;

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

            // (the pet: PartyBotAI::UpdateAI, SummonPetIfNeeded)
            Unit* pVictim = me->GetVictim();
            if (!pVictim)
                return;

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

            // Melee auto attack stays on (header step 0): the victim can be set without it
            // (Unit::Attack meleeAttack = false, a ranged victim after Feign Death), and then a
            // queued Raptor Strike never swings. No chase: it only swings in reach, and not while
            // Auto Shot runs (Unit::UpdateMeleeAttackingState).
            if (!me->HasUnitState(UNIT_STATE_MELEE_ATTACKING))
                me->Attack(pVictim, true);

            ChaseWhenOutOfRange(pVictim);

            AutoShot(pVictim);

            if (pVictim->IsMoving() && pVictim->GetVictim() == me && Cast(pVictim, s.pConcussiveShot))
                return;

            bool const lowMana = me->GetPowerPercent(POWER_MANA) < LOW_MANA_PCT;
            SpellEntry const* pAimed = lowMana ? s.pAimedShotRank1 : s.pAimedShot;
            if (AimedShotFits(pAimed) && Cast(pVictim, pAimed))
                return;
            if (Cast(pVictim, s.pMultiShotRank1))
                return;
            if (!lowMana && Cast(pVictim, s.pSerpentSting))
                return;

            if (pVictim->CanReachWithMeleeAutoAttack(me))
            {
                // (no return: melee abilities are on the next swing)
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
            // Auto Shot checks the facing (SPELL_CUSTOM_FACE_TARGET): turn first.
            if (!me->IsFacingTarget(pVictim))
                m_bot.FaceTarget(pVictim);
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

        // Header step 4: no Auto Shot running, an Auto Shot just went off, or the next one is
        // due after the cast and its wind-up anyway (a slow bow).
        bool AimedShotFits(SpellEntry const* pAimed) const
        {
            if (!pAimed)
                return false;
            Player* me = m_bot.me;
            if (!me->GetCurrentSpell(CURRENT_AUTOREPEAT_SPELL))
                return true;
            uint32 const left = me->GetAttackTimer(RANGED_ATTACK);
            uint32 const cycle = me->GetAttackTime(RANGED_ATTACK);
            if (left + AIMED_AFTER_SHOT_MS >= cycle)
                return true;
            return left >= uint32(std::max(0, int32(pAimed->GetCastTime(me)))) + AUTO_SHOT_WINDUP_MS;
        }
    };
}

std::unique_ptr<ClassRotation> ClassRotations::MakeHunter(PartyBotAI& bot)
{
    return std::make_unique<HunterRotation>(bot);
}
