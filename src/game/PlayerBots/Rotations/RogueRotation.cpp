/*
 * Rogue rotation (moved out of PartyBotAI.cpp 2026-11-14), combat swords (premade specs of
 * every level since 2026-11-15).
 *
 * Out of combat: main and off hand poisons, Stealth (ClassRotation::KeepStealth); with a victim
 * (an opener): the combat steps.
 *
 * In combat:
 *   1. Stealthed: Premeditation, then Cheap Shot on a caster (the stun stops its cast), Ambush
 *      (a dagger only) or Cheap Shot otherwise. Garrote is not used: in 1.12 it is only a bleed,
 *      its silence came with TBC.
 *   2. Slice and Dice with 1+ combo points when it is down or ends within SND_REFRESH_MS and
 *      the fight lasts at least SND_MIN_FIGHT_SEC more (Value::FightRemainingSec: the buff
 *      stays on through target switches, so the whole fight counts, not the victim's health).
 *   3. Eviscerate at 5 combo points, or with KILL_SHOT_MIN_POINTS+ when the victim dies within
 *      KILL_SHOT_HORIZON_MS (FinishForecast::DeathMs: the party's measured damage on it, ours
 *      too): the points die with it.
 *   4. Backstab, Ghostly Strike, Hemorrhage, Sinister Strike (Backstab, Hemorrhage, Ghostly
 *      Strike only for characters with their own talents and daggers; the server checks in
 *      PartyBotAI::PassesServerCastChecks skip Backstab without a dagger / not behind).
 * Without a Commander (no party snapshot): Slice and Dice above 10% victim health, no kill shot.
 *
 * Sprint is no longer cast here (until 2026-11-15: whenever the victim was out of melee reach,
 * its 5 min cooldown went on a few yards).
 *
 * The Commander decides: Vanish, Evasion, Feint (team rescue), Blind and Sap (crowd control;
 * Sap's stealth: Takeover::Stealth), Kick and Gouge (InterruptBehavior), Adrenaline Rush, Cold
 * Blood, Blade Flurry (BurstBehavior), Eviscerate before a target switch (TargetBehavior),
 * Kidney Shot on a runner and Sprint to catch one (flee interception), Preparation (team rescue,
 * RenewSurvivalBehavior).
 */

#include "Rotations/ClassRotation.h"
#include "PartyBotAI.h"
#include "Player.h"
#include "SpellAuras.h"
#include "Timer.h"
#include "Commander/CommanderMgr.h"
#include "Commander/PartyCommander.h"
#include "Commander/Perception/Forecast/FinishForecast.h"
#include "Commander/Perception/Values/ValueModel.h"

namespace
{
    uint32 const SND_REFRESH_MS = 2000;
    float const SND_MIN_FIGHT_SEC = 8.0f;
    float const SND_NO_COMMANDER_MIN_PCT = 10.0f;
    // Two rogue global cooldowns: one more builder and the finisher would not both land.
    uint32 const KILL_SHOT_HORIZON_MS = 2000;
    // 1 point Eviscerate does less than the Sinister Strike it replaces.
    uint8 const KILL_SHOT_MIN_POINTS = 2;
    uint8 const MAX_POINTS = 5;

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
                    if (Cast(pVictim, s.pCheapShot))
                        return;
                }
                else if (Cast(pVictim, s.pAmbush) || Cast(pVictim, s.pCheapShot))
                    return;
            }

            std::shared_ptr<Commander::PartyCommander> commander = sCommanderMgr.FindCommander(me);
            Commander::WorldModel const* world = commander ? &commander->GetWorld() : nullptr;

            uint8 const points = me->GetComboTargetGuid() == pVictim->GetObjectGuid() ? me->GetComboPoints() : 0;

            if (points && SliceAndDiceDue(world, pVictim) && Cast(pVictim, s.pSliceAndDice))
                return;

            if ((points >= MAX_POINTS || (points >= KILL_SHOT_MIN_POINTS && DiesSoon(world, pVictim))) &&
                Cast(pVictim, s.pEviscerate))
                return;

            if (Cast(pVictim, s.pBackstab))
                return;
            if (Cast(pVictim, s.pGhostlyStrike))
                return;
            if (Cast(pVictim, s.pHemorrhage))
                return;
            Cast(pVictim, s.pSinisterStrike);
        }

    private:
        bool SliceAndDiceDue(Commander::WorldModel const* world, Unit const* pVictim) const
        {
            SpellEntry const* snd = m_bot.m_spells.rogue.pSliceAndDice;
            if (!snd)
                return false;
            if (SpellAuraHolder const* holder = m_bot.me->GetSpellAuraHolder(snd->Id))
                if (holder->GetAuraDuration() < 0 || uint32(holder->GetAuraDuration()) > SND_REFRESH_MS)
                    return false;
            if (!world)
                return pVictim->GetHealthPercent() > SND_NO_COMMANDER_MIN_PCT;
            // No enemy health in the snapshot yet (just pulled): the fight is still ahead.
            if (world->rates.enemyHealth <= 0.0f)
                return true;
            return Commander::Value::FightRemainingSec(*world) >= SND_MIN_FIGHT_SEC;
        }

        bool DiesSoon(Commander::WorldModel const* world, Unit const* pVictim) const
        {
            if (!world)
                return false;
            Commander::EnemyFacts const* enemy = world->FindEnemy(pVictim->GetObjectGuid());
            if (!enemy)
                return false;
            // DeathMs counts from the snapshot; the time since then has passed already.
            uint32 const sinceMs = WorldTimer::getMSTimeDiff(world->refreshedAtMs, WorldTimer::getMSTime());
            uint32 const horizonMs = sinceMs + KILL_SHOT_HORIZON_MS;
            return Commander::FinishForecast::DeathMs(*world, *enemy, m_bot.me->GetObjectGuid(), 0, horizonMs) != 0;
        }
    };
}

std::unique_ptr<ClassRotation> ClassRotations::MakeRogue(PartyBotAI& bot)
{
    return std::make_unique<RogueRotation>(bot);
}
