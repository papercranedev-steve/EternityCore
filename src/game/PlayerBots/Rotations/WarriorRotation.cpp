/*
 * Damage warrior rotation (moved out of PartyBotAI.cpp 2026-11-14). Tanks:
 * Rotations/WarriorTankRotation.cpp.
 *
 * Out of combat: the home stance only. Battle Shout and Bloodrage moved into the fight (user,
 * 2026-11-15): out of combat they were cast after every fight.
 *
 * Home stance (review 2026-11-26): Berserker Stance once Bloodthirst is learned (fury), Battle
 * Stance otherwise (arms and levelling: Overpower and Rend need it). Not while a Commander swap
 * waits (TeamBotAI::DoCastWithStance: Sweeping Strikes, Retaliation from Berserker Stance), and
 * only losing at most PB_STANCE_SWAP_MAX_RAGE_LOSS (the rage Tactical Mastery keeps counts),
 * as the Commander's swaps: above it the rage is spent first. Before: Berserker Stance every
 * tick for any spec, rage lost without Tactical Mastery, a Commander swap undone.
 *
 * In combat (rework 2026-11-15, level 60 fury dual wield 17/34/0):
 *   0. Bloodrage below 20 rage and above 50% health (no GCD).
 *   1. Back to the home stance; no Defensive Stance at low health (user: a damage warrior has
 *      no shield, -10% damage taken is not worth the rage).
 *   2. Execute below 20%, Overpower (Battle Stance only).
 *   3. Rend only without Bloodthirst (arms levelling), Mortal Strike, Bloodthirst.
 *   3b. Battle Shout when missing and rage above 30.
 *   4. Whirlwind.
 *   5. Rage dump, Cleave (2+ enemies next to the victim) or Heroic Strike: above 30 rage and
 *      above what Mortal Strike / Bloodthirst / Whirlwind need when one of them is ready within
 *      1.5 s; not in the Execute phase (Execute takes the rage).
 *   No victim: Battle Shout when missing.
 *
 * Gone 2026-11-26: Intercept (the melee stance module's, ClassKit::GapCloser: it moved the bot
 * past the movement hold, the stance module's spot and route checks, and its stun kept an
 * enemy walking to the tank where it was).
 * Gone 2026-11-26: Charge on a victim out of combat. The rotation runs before the Commander's
 * hook there, and the "no charge" rule of a pull is the pull tank's only, so a damage warrior
 * with a victim opened the fight or ran into the pack the tank was bringing back. Charge is the
 * pull module's (ClassKit::OpenerCharge, as for the tank since 2026-11-02).
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
            // No Battle Shout / Bloodrage here (user, 2026-11-15): rage decays out of combat, so every
            // shout that ran out cost a Bloodrage between fights. Both moved into the fight.
            // (no Charge: the pull module's, header)
            KeepHomeStance();
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

            if (KeepHomeStance())
                return;

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

            // (Intercept: the melee stance module's, ClassKit::GapCloser)
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
        // Berserker Stance for a Bloodthirst warrior, Battle Stance otherwise (header).
        SpellEntry const* HomeStance() const
        {
            auto const& w = m_bot.m_spells.warrior;
            if (w.pBloodthirst && w.pBerserkerStance)
                return w.pBerserkerStance;
            return w.pBattleStance;
        }

        // True = the stance spell was cast (as WarriorTankRotation::KeepDefensiveStance). Not while
        // a Commander swap waits, not losing more rage than the Commander's swaps may.
        bool KeepHomeStance()
        {
            TeamBotAI& bot = m_bot;
            Player* me = bot.me;
            SpellEntry const* pStance = HomeStance();
            if (!pStance || bot.m_stanceSwap.spell || me->HasAura(pStance->Id))
                return false;
            uint32 const rage = me->GetPower(POWER_RAGE);
            if (rage - std::min(rage, bot.RageKeptOnStanceSwap()) > TeamBotAI::PB_STANCE_SWAP_MAX_RAGE_LOSS)
                return false;
            return Cast(me, pStance);
        }

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
