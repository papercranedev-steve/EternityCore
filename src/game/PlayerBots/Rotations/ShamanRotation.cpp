/*
 * Shaman rotation (moved out of PartyBotAI.cpp 2026-11-14).
 *
 * Out of combat: weapon buff, Lightning Shield, the heal step (healer: Commander RestTopUp);
 * with a victim (an opener): totems, then the combat steps.
 *
 * In combat:
 *   1. Heals the team: the heal step (the Commander heal module's order, none = rest), totems
 *      in the gaps, then its damage step (a healer: its tier, HealerDamage; a side healer that
 *      also deals damage for the team: Damage).
 *   2. Others: totems, then Damage: Stormstrike, Chain Lightning (not in melee), Flame Shock,
 *      Lightning Bolt (not a melee role, or the victim out of melee reach).
 * Flame Shock shares the shocks' cooldown: not while the plan keeps this shaman's Earth Shock
 * for an interrupt (ClassRotation::Cast, Constraints::InterruptKept).
 *
 * Totems (KeepTotems): the brain's choice per element (Coordination/Duties/TotemAssigner.h).
 * One is dropped when its element is empty, holds another totem or was left behind
 * (CombatBotBaseAI::KeepShamanTotem). Before the damage step: a damage dealer always has a
 * Lightning Bolt to cast, so a lost totem never came back (until 2026-11-26).
 *
 * The Commander decides: Earth Shock on a cast (InterruptBehavior), Frost Shock on a fleeing
 * enemy (FleeStopBehavior), Mana Tide (BoostBehavior), Elemental Mastery (BurstBehavior),
 * Nature's Swiftness (heal boosts), dispels, every heal (self heals included).
 * Removed 2026-11-26 (decisions the rotation made on its own): Frost Shock on any moving
 * victim (mostly a pulled enemy walking to the tank), Purge on any buff, a self heal below 20%
 * health, the pre-heal, percent heal thresholds, and the totem choice without the brain.
 */

#include "Rotations/ClassRotation.h"
#include "TeamBotAI.h"
#include "Player.h"
#include "SpellMgr.h"
#include "Commander/CommanderMgr.h"
#include "Commander/PartyCommander.h"
#include "Commander/Coordination/TeamPlan.h"

namespace
{
    class ShamanRotation : public ClassRotation
    {
    public:
        using ClassRotation::ClassRotation;

        void UpdateOutOfCombat() override
        {
            TeamBotAI& bot = m_bot;
            Player* me = bot.me;
            auto const& s = bot.m_spells.shaman;

            if (s.pWeaponBuff && bot.CanTryToCastSpell(me, s.pWeaponBuff) &&
                bot.CastWeaponBuff(s.pWeaponBuff, EQUIPMENT_SLOT_MAINHAND) == SPELL_CAST_OK)
                return;

            if (Cast(me, s.pLightningShield))
                return;

            if (bot.HealsForTeam() && bot.FindAndHealInjuredAlly())
                return;

            if (me->GetVictim())
            {
                if (KeepTotems())
                    return;
                UpdateInCombat();
            }
        }

        void UpdateInCombat() override
        {
            TeamBotAI& bot = m_bot;
            Player* me = bot.me;

            // A healer heals before it drops totems: up to four totems are four GCDs without a
            // heal. The heal module orders nothing while nobody needs a lift: the totems go
            // down in those gaps.
            if (bot.HealsForTeam())
            {
                if (bot.FindAndHealInjuredAlly())
                    return;
                if (KeepTotems())
                    return;
                if (Unit* pVictim = me->GetVictim())
                {
                    if (bot.GetRole() == ROLE_HEALER)
                        HealerDamage(pVictim, bot.HealerDamageNow());
                    else if (bot.DealsDamageForTeam())
                        Damage(pVictim);
                }
                return;
            }

            if (KeepTotems())
                return;

            if (Unit* pVictim = me->GetVictim())
                Damage(pVictim);
        }

    private:
        // A healer's damage step by its tier this tick (healer damage tiers doc 2026-11-21,
        // section 6). Its melee swings (FREE) need it next to the victim (step 5).
        //   SPARE and FULL  Flame Shock; Lightning Bolt out of melee reach
        void HealerDamage(Unit* pVictim, uint8 tier)
        {
            if (tier < Commander::HEALER_DAMAGE_SPARE)
                return;
            auto const& s = m_bot.m_spells.shaman;
            if (Cast(pVictim, s.pFlameShock))
                return;
            if (!m_bot.me->CanReachWithMeleeAutoAttack(pVictim))
                Cast(pVictim, s.pLightningBolt);
        }

        // One totem dropped (true), elements in the order water, earth, air, fire.
        bool KeepTotems()
        {
            TeamBotAI& bot = m_bot;
            std::shared_ptr<Commander::PartyCommander> const commander = sCommanderMgr.FindCommander(bot.me);
            if (!commander)
                return false;
            static TotemSlot const SLOTS[] = { TOTEM_SLOT_WATER, TOTEM_SLOT_EARTH, TOTEM_SLOT_AIR, TOTEM_SLOT_FIRE };
            for (TotemSlot slot : SLOTS)
            {
                uint32 const spellId = commander->GetPlan().totems.TotemFor(bot.me->GetObjectGuid(), slot);
                if (spellId && bot.KeepShamanTotem(slot, sSpellMgr.GetSpellEntry(spellId)))
                    return true;
            }
            return false;
        }

        bool Damage(Unit* pVictim)
        {
            TeamBotAI& bot = m_bot;
            auto const& s = bot.m_spells.shaman;
            // A melee role stays on its swings: a 2.5 s cast stops them.
            bool const meleeRole = bot.GetRole() == ROLE_MELEE_DPS || bot.GetRole() == ROLE_TANK;
            bool const inReach = bot.me->CanReachWithMeleeAutoAttack(pVictim);

            if (Cast(pVictim, s.pStormstrike))
                return true;
            if (!meleeRole && Cast(pVictim, s.pChainLightning))
                return true;
            if (Cast(pVictim, s.pFlameShock))
                return true;
            if ((!meleeRole || !inReach) && Cast(pVictim, s.pLightningBolt))
                return true;
            return false;
        }
    };
}

std::unique_ptr<ClassRotation> ClassRotations::MakeShaman(TeamBotAI& bot)
{
    return std::make_unique<ShamanRotation>(bot);
}
