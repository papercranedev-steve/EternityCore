/*
 * Mage rotation (user, 2026-11-01, after strat-20261101-235124; review 2026-11-26).
 *
 * Out of combat: Arcane Intellect / Brilliance (CombatBotBaseAI::SelectBuffTarget: the team,
 * those with mana), then the armor: Mage Armor when someone else heals the team (mana
 * regeneration while casting; the healer takes care of the damage), else Ice Armor. Armors
 * are only put on out of combat, so they are never swapped mid-fight. With nothing else to do,
 * a mana gem when it has none (ConjureManaGem). No opener here: this step runs before the
 * Commander hook; opening on the team's hunt target is TeamBotAI::FightVictim (IsOpeningOn).
 *
 * In combat, in this order:
 *   1. No area spells (user, 2026-11-02): the Commander area damage module decides when, which
 *      and where (Commander/Behaviors/Combat/AreaDamageBehavior.h).
 *   2. Pyroblast while Presence of Mind is on (whoever cast it: BurstBehavior pairs them).
 *   3. The filler, or on a victim immune to it the next of ClassKit::Fillers (Frostbolt /
 *      Fireball, Arcane Missiles).
 *   4. Wand when out of mana.
 * The Commander decides: Ice Barrier and Mana Shield (EscapeBehavior), Ice Block, and Cold
 * Snap only to reset it (team rescue), Blink / Frost Nova / running from melee
 * (EscapeBehavior), Counterspell (InterruptBehavior), Polymorph (crowd control), Combustion,
 * Arcane Power, Presence of Mind + Pyroblast (BurstBehavior).
 * No Fire Blast (user 2026-11-14, strat-20261114-180150: "while moving" took the GCD just as
 * the area caster needed Frost Nova; how to use it is decided later).
 * Evocation is the Commander's call (ManaBudgetBehavior self restore), not the rotation's.
 * Scorch is not cast: without Improved Scorch it does less per cast than the filler.
 *
 * Removed 2026-11-26 (review): the upstream Pyroblast opener (victim not in combat, more max
 * health than the mage, beyond 30 yd: who opens is the pull's / hunt's), Ice Barrier whenever
 * something attacked (now valued by EscapeBehavior), the armor by team size, the dead
 * distancing check (nothing starts that movement since RunAwayFromTarget went).
 */

#include "Rotations/ClassRotation.h"
#include "Commander/CommanderMgr.h"
#include "Commander/PartyCommander.h"
#include "Commander/Perception/WorldModel.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Team.h"
#include "TeamBotAI.h"
#include "Commander/Classes/ClassKit.h"
#include "Player.h"
#include "Map.h"
#include "Spell.h"

namespace
{
    float const WAND_MANA_PCT = 5.0f;
    float const CONJURE_MIN_MANA_PCT = 50.0f;

    class MageRotation : public ClassRotation
    {
    public:
        using ClassRotation::ClassRotation;

        void UpdateOutOfCombat() override
        {
            TeamBotAI& bot = m_bot;
            Player* me = bot.me;
            auto const& s = bot.m_spells.mage;

            SpellEntry const* pBuffSpell = nullptr;
            if (Player* pTarget = bot.SelectBuffTarget(s.pArcaneIntellect, s.pArcaneBrilliance, pBuffSpell))
            {
                if (Buff(pTarget, pBuffSpell))
                    return;
            }

            if (Buff(me, ChooseArmor()))
                return;

            EndBuffing(s.pArcaneIntellect);

            if (!me->GetVictim())
                ConjureManaGem();
        }

        // Nothing else to do (user 2026-11-15): conjure the best mana gem it knows when it has
        // none, while it and its team's leader stand still, the team neither fights nor pulls,
        // and the mana stays above CONJURE_MIN_MANA_PCT (resting refills the rest).
        // ManaBudgetBehavior uses the gem.
        bool ConjureManaGem()
        {
            TeamBotAI& bot = m_bot;
            Player* me = bot.me;
            SpellEntry const* pConjure = bot.m_spells.mage.pConjureManaGem;
            if (!pConjure || me->IsMoving() || me->IsInCombat())
                return false;
            uint32 gem = 0;
            for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
                if (pConjure->Effect[i] == SPELL_EFFECT_CREATE_ITEM)
                    gem = uint32(pConjure->EffectItemType[i]);
            if (!gem || me->HasItemCount(gem, 1))
                return false;
            if (TeamBusy())
                return false;
            uint32 const maxMana = me->GetMaxPower(POWER_MANA);
            uint32 const cost = uint32(Spell::CalculatePowerCost(pConjure, me));
            if (!maxMana || me->GetPower(POWER_MANA) < cost ||
                float(me->GetPower(POWER_MANA) - cost) * 100.0f / float(maxMana) < CONJURE_MIN_MANA_PCT)
                return false;
            return Cast(me, pConjure);
        }

        void UpdateInCombat() override
        {
            TeamBotAI& bot = m_bot;
            Player* me = bot.me;
            auto const& s = bot.m_spells.mage;

            Unit* pVictim = me->GetVictim();
            if (!pVictim)
                return;

            ChaseWhenOutOfRange(pVictim);

            if (s.pPyroblast && s.pPresenceOfMind && me->HasAura(s.pPresenceOfMind->Id) &&
                Cast(pVictim, s.pPyroblast))
                return;

            if (SpellEntry const* pFiller = FillerFor(pVictim))
                if (Cast(pVictim, pFiller))
                    return;

            ShootWandBelowMana(pVictim, WAND_MANA_PCT);
        }

    private:
        // The first of ClassKit::Fillers the victim is not immune to (a frost immune elemental:
        // Fireball). Only immunity moves on: a filler that cannot go now for another reason
        // (cooldown, range, the plan's cast rules) is not replaced by a worse spell.
        SpellEntry const* FillerFor(Unit const* pVictim) const
        {
            for (SpellEntry const* pSpell : m_bot.Kit().Fillers())
                if (pSpell && !pVictim->IsImmuneToSpell(pSpell, false))
                    return pSpell;
            return nullptr;
        }

        // Mage Armor (mana while casting) when someone else heals the team, by functions and
        // not by team size (a mage and a warrior have no healer); else Ice Armor (Frost Armor
        // at low level). The other one when only one is known.
        SpellEntry const* ChooseArmor() const
        {
            auto const& s = m_bot.m_spells.mage;
            if (OthersHeal())
                return s.pMageArmor ? s.pMageArmor : s.pIceArmor;
            return s.pIceArmor ? s.pIceArmor : s.pMageArmor;
        }

        // Another member of the team has the heal function (TeamPlan::coverage, players by
        // their role). Without a Commander: nobody.
        bool OthersHeal() const
        {
            Player* me = m_bot.me;
            std::shared_ptr<Commander::PartyCommander> commander = sCommanderMgr.FindCommander(me);
            if (!commander)
                return false;
            for (Commander::PartyMemberFacts const& m : commander->GetWorld().party)
                if (!m.isPet && m.guid != me->GetObjectGuid() && (m.functions & Commander::FUNCTION_HEAL))
                    return true;
            return false;
        }

        // A 3 s conjure now would be in the way: the team fights or pulls, or its leader (a
        // player or the first bot of a population unit) walks on or fights.
        bool TeamBusy() const
        {
            Player* me = m_bot.me;
            if (std::shared_ptr<Commander::PartyCommander> commander = sCommanderMgr.FindCommander(me))
                if (commander->GetWorld().HasPartyEnemy() ||
                    commander->GetPlan().pull.phase != Commander::PullPhase::None)
                    return true;
            ObjectGuid const leaderGuid = Commander::Team::LeaderOf(me);
            if (leaderGuid.IsEmpty() || leaderGuid == me->GetObjectGuid())
                return false;
            Player* pLeader = me->GetMap()->GetPlayer(leaderGuid);
            return pLeader && (pLeader->IsMoving() || pLeader->IsInCombat());
        }
    };
}

std::unique_ptr<ClassRotation> ClassRotations::MakeMage(TeamBotAI& bot)
{
    return std::make_unique<MageRotation>(bot);
}
