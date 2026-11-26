/*
 * Rogue rotation (moved out of PartyBotAI.cpp 2026-11-14), combat swords (premade specs of
 * every level since 2026-11-15).
 *
 * Out of combat: main and off hand poisons (a companion's vials only while the Commander has
 * nothing for it to do: no fight, pull or hunt), a Stealth no module wants is cancelled
 * (ClassRotation::DropUnwantedStealth); with a victim (an opener): the combat steps.
 *
 * In combat:
 *   1. Stealthed (the Commander put it there: HuntBehavior's opener; not the Sap controller,
 *      TeamBotAI::IsStealthControlled): Premeditation, then Ambush (a dagger, behind) or Cheap
 *      Shot. Garrote is not used: in 1.12 it is only a bleed, its silence came with TBC.
 *   2. Slice and Dice with 1+ combo points when it is down or ends within SND_REFRESH_MS and
 *      the fight lasts at least SND_MIN_FIGHT_SEC more (Value::FightRemainingSec: the buff
 *      stays on through target switches, so the whole fight counts, not the victim's health).
 *   3. Eviscerate at 5 combo points, or with KILL_SHOT_MIN_POINTS+ when the victim dies within
 *      KILL_SHOT_HORIZON_MS (FinishForecast::DeathMs: the party's measured damage on it, ours
 *      too): the points die with it.
 *   4. Backstab, Ghostly Strike, Hemorrhage, Sinister Strike (Ghostly Strike and Hemorrhage
 *      are talents: only characters with their own talents have them; Backstab needs a dagger
 *      and the back, the server checks in TeamBotAI::PassesServerCastChecks skip it otherwise).
 * Without a Commander snapshot: no forecast, Slice and Dice when due, no kill shot.
 *
 * Sprint is no longer cast here (until 2026-11-15: whenever the victim was out of melee reach,
 * its 5 min cooldown went on a few yards). Taken out 2026-11-26: going into Stealth itself (on
 * every victim out of combat, below 10% health, with the leader dead / feigning / stealthed);
 * Cheap Shot on a caster as an interrupt the Commander did not know of (InterruptBehavior
 * assigns interrupts; Unit::IsCaster is the creature's class, not a cast).
 *
 * The Commander decides: Vanish, Evasion, Feint (team rescue), Blind and Sap (crowd control;
 * Sap's stealth: Takeover::Stealth), Kick and Gouge (InterruptBehavior), Adrenaline Rush, Cold
 * Blood, Blade Flurry (BurstBehavior), Eviscerate before a target switch (TargetBehavior),
 * Kidney Shot on a runner and Sprint to catch one (flee interception), Preparation (team rescue,
 * RenewSurvivalBehavior).
 */

#include "Rotations/ClassRotation.h"
#include "TeamBotAI.h"
#include "Player.h"
#include "SpellAuras.h"
#include "Timer.h"
#include "Commander/CommanderMgr.h"
#include "Commander/PartyCommander.h"
#include "Commander/Perception/Forecast/FinishForecast.h"
#include "Commander/Perception/Values/ValueModel.h"
#include "Shared/Poisons.h"
#include "Item.h"

namespace
{
    uint32 const SND_REFRESH_MS = 2000;
    float const SND_MIN_FIGHT_SEC = 8.0f;
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
            TeamBotAI& bot = m_bot;
            Player* me = bot.me;
            auto const& s = bot.m_spells.rogue;

            if (bot.GetsFreeSupplies())
            {
                if (s.pMainHandPoison && bot.CanTryToCastSpell(me, s.pMainHandPoison) &&
                    bot.CastWeaponBuff(s.pMainHandPoison, EQUIPMENT_SLOT_MAINHAND) == SPELL_CAST_OK)
                    return;

                if (s.pOffHandPoison && bot.CanTryToCastSpell(me, s.pOffHandPoison) &&
                    bot.CastWeaponBuff(s.pOffHandPoison, EQUIPMENT_SLOT_OFFHAND) == SPELL_CAST_OK)
                    return;
            }
            else if (IdleForCommander() &&
                     (PoisonFromBags(EQUIPMENT_SLOT_MAINHAND) || PoisonFromBags(EQUIPMENT_SLOT_OFFHAND)))
                return;

            DropUnwantedStealth(s.pStealth);

            if (me->GetVictim())
                UpdateInCombat();
        }

        void UpdateInCombat() override
        {
            TeamBotAI& bot = m_bot;
            Player* me = bot.me;
            auto const& s = bot.m_spells.rogue;

            Unit* pVictim = me->GetVictim();
            if (!pVictim)
                return;

            if (me->HasAuraType(SPELL_AURA_MOD_STEALTH) && !bot.IsStealthControlled())
            {
                // (no return: Premeditation is off the global cooldown)
                Cast(pVictim, s.pPremeditation);

                if (Cast(pVictim, s.pAmbush) || Cast(pVictim, s.pCheapShot))
                    return;
            }

            std::shared_ptr<Commander::PartyCommander> commander = sCommanderMgr.FindCommander(me);
            Commander::WorldModel const* world = commander ? &commander->GetWorld() : nullptr;

            uint8 const points = me->GetComboTargetGuid() == pVictim->GetObjectGuid() ? me->GetComboPoints() : 0;

            if (points && SliceAndDiceDue(world) && Cast(pVictim, s.pSliceAndDice))
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
        // A bot without free supplies (a companion) puts the best poison vial it carries on a
        // weapon without one (the vials are made by Life/Errands/CraftRun). Not while moving:
        // the vial has a cast time.
        bool PoisonFromBags(EquipmentSlots slot)
        {
            Player* me = m_bot.me;
            if (me->IsMoving() || me->IsNonMeleeSpellCasted(false))
                return false;
            Item* pWeapon = me->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
            if (!pWeapon || pWeapon->GetProto()->Class != ITEM_CLASS_WEAPON ||
                pWeapon->GetEnchantmentId(TEMP_ENCHANTMENT_SLOT))
                return false;
            Item* pVial = BotShared::Poisons::BestInBags(me);
            return pVial && m_bot.UseBagItemOnItem(pVial->GetEntry(), pWeapon);
        }

        // A vial's 3 s cast makes PartyBotAI::UpdateAI return before the Commander hook (the
        // out of combat rotation runs first): only while the Commander has nothing for the bot
        // (PartyCommander::WantsOutOfCombatControl: no fight, pull, hunt, retreat or walk).
        bool IdleForCommander() const
        {
            if (m_bot.me->GetVictim())
                return false;
            std::shared_ptr<Commander::PartyCommander> commander = sCommanderMgr.FindCommander(m_bot.me);
            return !commander || !commander->WantsOutOfCombatControl();
        }

        bool SliceAndDiceDue(Commander::WorldModel const* world) const
        {
            SpellEntry const* snd = m_bot.m_spells.rogue.pSliceAndDice;
            if (!snd)
                return false;
            if (SpellAuraHolder const* holder = m_bot.me->GetSpellAuraHolder(snd->Id))
                if (holder->GetAuraDuration() < 0 || uint32(holder->GetAuraDuration()) > SND_REFRESH_MS)
                    return false;
            // No snapshot, or no enemy health in it yet (just pulled): the fight is still ahead.
            if (!world || world->rates.enemyHealth <= 0.0f)
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

std::unique_ptr<ClassRotation> ClassRotations::MakeRogue(TeamBotAI& bot)
{
    return std::make_unique<RogueRotation>(bot);
}
