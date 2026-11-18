/*
 * Damage warrior rotation (moved out of PartyBotAI.cpp 2026-11-14). Tanks:
 * Rotations/WarriorTankRotation.cpp.
 *
 * Out of combat: Battle Stance, Charge an opener victim. Battle Shout and Bloodrage moved into
 * the fight (user, 2026-11-15): out of combat they were cast after every fight.
 *
 * In combat (rework 2026-11-15, level 60 fury dual wield 17/34/0):
 *   0. Bloodrage below 20 rage and above 50% health (no GCD).
 *   1. Berserker Stance first (Tactical Mastery keeps 25 rage); no Defensive Stance at low
 *      health (user: a damage warrior has no shield, -10% damage taken is not worth the rage).
 *   2. Execute below 20%, Overpower (Battle Stance only: levelling arms before Berserker Stance).
 *   3. Rend only without Bloodthirst (arms levelling), Mortal Strike, Bloodthirst.
 *   3b. Battle Shout when missing and rage above 30.
 *   4. Intercept, Whirlwind.
 *   5. Rage dump, Cleave (2+ enemies next to the victim) or Heroic Strike: above 30 rage and
 *      above what Mortal Strike / Bloodthirst / Whirlwind need when one of them is ready within
 *      1.5 s; not in the Execute phase (Execute takes the rage).
 *   No victim: Battle Shout when missing.
 *
 * Gone 2026-11-15: Hamstring on any moving victim (a pack walking to the tank moves too; the
 * fleeing ones are the Commander's FleeStopBehavior), Defensive Stance below 20% health and with
 * it Shield Block / Shield Slam / Disarm (Defensive Stance only), Concussion Blow (a protection
 * talent; the Commander still casts it on a cast, InterruptBehavior).
 *
 * The Commander decides: Pummel, Shield Bash, Concussion Blow on a cast (InterruptBehavior),
 * Last Stand, Shield Wall (team rescue), Intimidating Shout (crowd control), Retaliation,
 * Sweeping Strikes, Recklessness, Death Wish (BurstBehavior), Hamstring on a fleeing enemy
 * (FleeStopBehavior). Berserker Rage on a fear: TeamBotAI::BreakControl (ClassKit::FearBreak).
 */

#include "Rotations/ClassRotation.h"
#include "TeamBotAI.h"
#include "Player.h"
#include "Spell.h"
#include <algorithm>
#include <initializer_list>

namespace
{
    uint32 const RAGE_DUMP = 300;               // 30 rage
    float const CLEAVE_RADIUS = 8.0f;
    uint32 const BLOODRAGE_BELOW_RAGE = 200;    // 20 rage
    float const BLOODRAGE_MIN_HEALTH_PCT = 50.0f;
    float const EXECUTE_PHASE_PCT = 20.0f;
    int32 const STRIKE_RESERVE_MS = 1500;       // a main strike ready this soon keeps its rage

    class WarriorRotation : public ClassRotation
    {
    public:
        using ClassRotation::ClassRotation;

        void UpdateOutOfCombat() override
        {
            TeamBotAI& bot = m_bot;
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
            TeamBotAI& bot = m_bot;
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

            // Berserker Stance right after the Charge (no return: the next spell goes off in it).
            Cast(me, w.pBerserkerStance);

            bool const executePhase = w.pExecute && pVictim->GetHealthPercent() < EXECUTE_PHASE_PCT;
            if (executePhase && Cast(pVictim, w.pExecute))
                return;
            if (Cast(pVictim, w.pOverpower))
                return;

            // Rend: an arms opener, a Bloodthirst warrior spends the rage on Bloodthirst.
            if (!w.pBloodthirst && Cast(pVictim, w.pRend))
                return;
            if (Cast(pVictim, w.pMortalStrike))
                return;
            if (Cast(pVictim, w.pBloodthirst))
                return;

            // Battle Shout after the main strikes, only with rage to spare (the Heroic Strike line),
            // so it never takes the rage Mortal Strike / Bloodthirst need.
            if (shoutMissing && rage > RAGE_DUMP && Cast(me, w.pBattleShout))
                return;

            if (Cast(pVictim, w.pIntercept))
                return;
            if (Cast(me, w.pWhirlwind))
                return;

            // (the chase: UpdateAI restarts an idle one before the rotation, or the Commander
            // melee stance module chases)

            if (executePhase)
                return;
            SpellEntry const* pDump = (w.pCleave && me->GetEnemyCountInRadiusAround(pVictim, CLEAVE_RADIUS) > 1) ?
                                       w.pCleave : w.pHeroicStrike;
            if (pDump && rage > std::max(RAGE_DUMP, StrikeReserve() + PowerCost(pDump)))
                Cast(pVictim, pDump);
        }

    private:
        uint32 PowerCost(SpellEntry const* pSpell) const
        {
            return Spell::CalculatePowerCost(pSpell, m_bot.me, nullptr, nullptr, false);
        }

        // Rage the main strikes about to come off cooldown need (Heroic Strike would starve
        // Bloodthirst: 5 man rage is short). Whirlwind only in Berserker Stance.
        uint32 StrikeReserve() const
        {
            Player* me = m_bot.me;
            auto const& w = m_bot.m_spells.warrior;
            uint32 reserve = 0;
            for (SpellEntry const* pStrike : { w.pMortalStrike, w.pBloodthirst, w.pWhirlwind })
            {
                if (!pStrike || pStrike->GetErrorAtShapeshiftedCast(me->GetShapeshiftForm()) != SPELL_CAST_OK)
                    continue;
                int32 const left = me->GetSpellCooldownLeft(pStrike);
                if (left >= 0 && left <= STRIKE_RESERVE_MS)
                    reserve += PowerCost(pStrike);
            }
            return reserve;
        }
    };
}

std::unique_ptr<ClassRotation> ClassRotations::MakeWarrior(TeamBotAI& bot)
{
    return std::make_unique<WarriorRotation>(bot);
}
