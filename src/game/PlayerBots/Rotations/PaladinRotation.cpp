/*
 * Paladin rotation (moved out of PartyBotAI.cpp 2026-11-14).
 *
 * Out of combat: aura, Righteous Fury (tank; cancelled on anyone else), blessings on the
 * party, the heal step (healer: Commander RestTopUp).
 * Aura and blessings: the brain says which (Commander/Coordination/Duties/BlessingAssigner.h,
 * TeamPlan::blessings, user 2026-11-15); none without that plan.
 *
 * In combat:
 *   1. Righteous Fury as above (made the tank in the fight: Commander CrisisAssigner).
 *   2. Attacked: Holy Shield.
 *   3. Heals the team: the heal step (the Commander heal module's order, none = rest), then its
 *      damage step (a healer: its tier, HealerDamage; a side healer that also deals damage for
 *      the team: Damage). Others: Damage: the judgement the brain gave it
 *      (Commander/Coordination/Duties/JudgementAssigner.h: Seal of Wisdom / Light, Judgement,
 *      then its own seal again; on a victim living JUDGEMENT_MIN_TTD_SEC+ without one of that
 *      kind), seal, Judgement, Hammer of Wrath on a victim below 20%, Consecration (not a
 *      tank), Holy Shock (not with the heal function), Exorcism on undead and demons, Holy
 *      Wrath on undead / demons.
 *   4. Blessing of Freedom when rooted or slowed.
 * Blessing of Sacrifice is not cast (user 2026-11-15: it took a GCD before any heal, even in an
 * emergency, and moved the damage onto the paladin).
 *
 * The Commander decides: Divine Shield, Blessing of Protection, Lay on Hands (team rescue),
 * Hammer of Justice on a cast (InterruptBehavior), as a hold (EscapeBehavior) and on a fleeing
 * enemy (FleeStopBehavior), Turn Undead (crowd control, ClassKit::Controls), Consecration for
 * a tank (ThreatBehavior, ClassKit::AreaThreat), dispels in combat (DispelBehavior), Divine
 * Favor and Holy Shock heals (heal module), every heal (self heals included).
 * Removed 2026-11-26 (decisions the rotation made on its own): a self heal below 30% health,
 * Hammer of Justice below 20% health (on its victim, not on what hit it), Turn Evil on a
 * second undead attacker (the 2.0 name: it never matched a 1.12 spell), Divine Favor spent on
 * a damage Holy Shock outside a healer's full damage tier, the pre-heal, percent heal
 * thresholds, and the aura and blessing choice without the brain.
 */

#include "Rotations/ClassRotation.h"
#include "Commander/Team.h"
#include "TeamBotAI.h"
#include "Player.h"
#include "Group.h"
#include "SpellMgr.h"
#include "SpellAuras.h"
#include "Commander/CommanderMgr.h"
#include "Commander/PartyCommander.h"
#include "Commander/Coordination/Duties/BlessingAssigner.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/WorldModel.h"
#include "Commander/Perception/Values/ValueModel.h"

namespace
{
    class PaladinRotation : public ClassRotation
    {
    public:
        using ClassRotation::ClassRotation;

        // A judgement is put on a victim expected to live at least this long (seal + Judgement:
        // two casts and their mana for a 10 s debuff the melee hits refresh).
        static constexpr float JUDGEMENT_MIN_TTD_SEC = 15.0f;

        void UpdateOutOfCombat() override
        {
            TeamBotAI& bot = m_bot;
            Player* me = bot.me;

            if (Cast(me, ChosenAura()))
                return;

            if (KeepRighteousFury())
                return;

            if (BlessParty())
                return;

            EndBuffing(m_lastBlessing);

            if (bot.HealsForTeam() && bot.FindAndHealInjuredAlly())
                return;
        }

        void UpdateInCombat() override
        {
            TeamBotAI& bot = m_bot;
            Player* me = bot.me;
            auto const& s = bot.m_spells.paladin;

            if (KeepRighteousFury())
                return;

            if (!me->GetAttackers().empty() && Cast(me, s.pHolyShield))
                return;

            // Heals the team (a healer, or nobody else heals): the heal step first, the damage
            // step only when it also deals damage for the team (nobody else does).
            if (bot.HealsForTeam())
            {
                if (bot.FindAndHealInjuredAlly())
                    return;
                if (bot.GetRole() == ROLE_HEALER)
                {
                    if (HealerDamage(bot.HealerDamageNow()))
                        return;
                }
                else if (bot.DealsDamageForTeam() && Damage())
                    return;
            }
            else if (Damage())
                return;

            if ((me->HasUnitState(UNIT_STATE_ROOT) || me->HasAuraType(SPELL_AURA_MOD_DECREASE_SPEED)) &&
                Cast(me, s.pBlessingOfFreedom))
                return;
        }

    private:
        SpellEntry const* m_lastBlessing = nullptr;     // EndBuffing

        // Righteous Fury (holy threat +60%) only on a tank: put up, and cancelled on anyone else
        // (until 2026-11-26 a paladin no longer the tank kept it for its 30 min and pulled
        // aggro; the Commander also counts it as a tank stance, PartyMemberFacts::tankStance).
        // True = cast.
        bool KeepRighteousFury()
        {
            SpellEntry const* pFury = m_bot.m_spells.paladin.pRighteousFury;
            if (!pFury)
                return false;
            if (m_bot.m_role == ROLE_TANK)
                return Cast(m_bot.me, pFury);
            if (m_bot.me->HasAura(pFury->Id))
                m_bot.me->RemoveAurasDueToSpellByCancel(pFury->Id);
            return false;
        }

        std::shared_ptr<Commander::PartyCommander> TeamCommander() const
        {
            return sCommanderMgr.FindCommander(m_bot.me);
        }

        SpellEntry const* ChosenAura() const
        {
            std::shared_ptr<Commander::PartyCommander> const commander = TeamCommander();
            if (!commander)
                return nullptr;
            uint32 const spellId = commander->GetPlan().blessings.AuraFor(m_bot.me->GetObjectGuid());
            return spellId ? sSpellMgr.GetSpellEntry(spellId) : nullptr;
        }

        // One blessing cast (true) on the first member that misses the one it gets from me.
        // Greater Blessings first: one lands on the whole class and replaces my plain blessing
        // on a member of it that gets another one from me (a tank among warriors).
        bool BlessParty()
        {
            std::shared_ptr<Commander::PartyCommander> const commander = TeamCommander();
            if (!commander)
                return false;
            return BlessParty(commander->GetPlan(), true) || BlessParty(commander->GetPlan(), false);
        }

        bool BlessParty(Commander::TeamPlan const& plan, bool greater)
        {
            TeamBotAI& bot = m_bot;
            Player* me = bot.me;
            for (Player* pMember : Commander::Team::Members(me))
            {
                if (!pMember || !me->IsValidHelpfulTarget(pMember) || pMember->IsGameMaster() ||
                    !me->IsWithinLOSInMap(pMember) || !me->IsWithinDist(pMember, 30.0f))
                    continue;
                uint32 const spellId = plan.blessings.BlessingFor(me->GetObjectGuid(), pMember->GetObjectGuid());
                SpellEntry const* spell = spellId ? sSpellMgr.GetSpellEntry(spellId) : nullptr;
                if (!spell || Commander::PaladinBuffs::IsGreaterBlessing(spell) != greater ||
                    !bot.IsValidBuffTarget(pMember, spell) || bot.IsBuffPendingFromOther(pMember, spell, nullptr))
                    continue;
                if (Buff(pMember, spell))
                {
                    m_lastBlessing = spell;
                    return true;
                }
            }
            return false;
        }

        enum class JudgeStep { None, Cast, Waiting };

        // The judgement the brain gave this paladin (JudgementAssigner, user 2026-11-15): on a
        // victim that lives long enough and has none of that kind from anyone, the seal for it,
        // then Judgement; the paladin's melee hits keep it up, the own seal comes back after.
        // Waiting: the seal is up, the Judgement is not cast yet (cooldown, out of its range).
        JudgeStep KeepJudgement(Unit* pVictim)
        {
            TeamBotAI& bot = m_bot;
            Player* me = bot.me;
            auto const& s = bot.m_spells.paladin;
            if (!pVictim || !s.pJudgement)
                return JudgeStep::None;
            std::shared_ptr<Commander::PartyCommander> const commander = TeamCommander();
            if (!commander)
                return JudgeStep::None;
            uint32 const sealId = commander->GetPlan().judgements.SealFor(me->GetObjectGuid());
            SpellEntry const* seal = sealId ? sSpellMgr.GetSpellEntry(sealId) : nullptr;
            if (!seal)
                return JudgeStep::None;

            Commander::JudgementKind const kind = Commander::PaladinBuffs::SealOf(seal);
            for (auto const& itr : pVictim->GetSpellAuraHolderMap())
                if (Commander::PaladinBuffs::JudgementOf(itr.second->GetSpellProto()) == kind)
                    return JudgeStep::None;     // up (mine, refreshed by my hits, or another paladin's)

            Commander::EnemyFacts const* enemy = commander->GetWorld().FindEnemy(pVictim->GetObjectGuid());
            if (!enemy || Commander::Value::EnemyTimeToDie(commander->GetWorld(), *enemy) < JUDGEMENT_MIN_TTD_SEC ||
                me->GetPowerPercent(POWER_MANA) <= 30.0f)
                return JudgeStep::None;

            bool const sealUp = me->HasAura(seal->Id);
            if (!me->IsSpellReady(s.pJudgement))
                return sealUp ? JudgeStep::Waiting : JudgeStep::None;
            if (!sealUp)
                return Cast(me, seal) ? JudgeStep::Cast : JudgeStep::None;
            return Cast(pVictim, s.pJudgement) ? JudgeStep::Cast : JudgeStep::Waiting;
        }

        // What Exorcism and Holy Wrath hit (until 2026-11-26 Exorcism only on undead).
        static bool IsUndeadOrDemon(Unit const* pVictim)
        {
            if (!pVictim->IsCreature())
                return false;
            uint32 const type = pVictim->GetCreatureType();
            return type == CREATURE_TYPE_UNDEAD || type == CREATURE_TYPE_DEMON;
        }

        // A healer's damage step by its tier this tick (healer damage tiers doc 2026-11-21,
        // section 6). True = cast. Its melee swings (FREE) need it next to the victim (step 5).
        //   SPARE  its seal while in melee reach (the swings use it), Exorcism on undead / demons
        //   FULL   + the brain's judgement seal, Judgement, Hammer of Wrath, Divine Favor +
        //          Holy Shock (a healing cooldown spent on damage: FULL only)
        bool HealerDamage(uint8 tier)
        {
            if (tier < Commander::HEALER_DAMAGE_SPARE)
                return false;
            TeamBotAI& bot = m_bot;
            Player* me = bot.me;
            auto const& s = bot.m_spells.paladin;
            Unit* pVictim = me->GetVictim();
            if (!pVictim)
                return false;
            bool const full = tier >= Commander::HEALER_DAMAGE_FULL;

            bool holdSeal = false;
            if (full)
            {
                JudgeStep const kept = KeepJudgement(pVictim);
                if (kept == JudgeStep::Cast)
                    return true;
                holdSeal = kept == JudgeStep::Waiting;
            }
            bool const hasSeal = s.pSeal && me->HasAura(s.pSeal->Id);
            if (!holdSeal && !hasSeal && s.pSeal && (full || me->CanReachWithMeleeAutoAttack(pVictim)) &&
                Cast(me, s.pSeal))
                return true;
            if (full)
            {
                if (!holdSeal && hasSeal && Cast(pVictim, s.pJudgement))
                    return true;
                if (pVictim->GetHealthPercent() < 20.0f && Cast(pVictim, s.pHammerOfWrath))
                    return true;
                if (s.pHolyShock && bot.CanTryToCastSpell(pVictim, s.pHolyShock))
                {
                    if (s.pDivineFavor && bot.CanTryToCastSpell(me, s.pDivineFavor))
                        bot.DoCastSpell(me, s.pDivineFavor);
                    if (bot.DoCastSpell(pVictim, s.pHolyShock) == SPELL_CAST_OK)
                        return true;
                }
            }
            return IsUndeadOrDemon(pVictim) && Cast(pVictim, s.pExorcism);
        }

        // True = cast.
        bool Damage()
        {
            TeamBotAI& bot = m_bot;
            Player* me = bot.me;
            auto const& s = bot.m_spells.paladin;

            Unit* pVictim = me->GetVictim();
            JudgeStep const kept = KeepJudgement(pVictim);
            if (kept == JudgeStep::Cast)
                return true;
            // Its seal is up for the judgement still to come: not replaced by the own one.
            bool const holdSeal = kept == JudgeStep::Waiting;

            // (until 2026-11-26 cast without returning: the same tick tried Judgement in its GCD)
            bool const hasSeal = s.pSeal && me->HasAura(s.pSeal->Id);
            if (!holdSeal && !hasSeal && Cast(me, s.pSeal))
                return true;

            if (!pVictim)
                return false;

            if (!holdSeal && hasSeal && me->GetPowerPercent(POWER_MANA) > 30.0f && Cast(pVictim, s.pJudgement))
                return true;

            if (pVictim->GetHealthPercent() < 20.0f && Cast(pVictim, s.pHammerOfWrath))
                return true;

            if (bot.m_role != ROLE_TANK && bot.GetAttackersInRangeCount(10.0f) > 2 && Cast(me, s.pConsecration))
                return true;

            // Holy Shock is the heal module's emergency heal (ClassKit::CombatHealUse) and Divine
            // Favor its heal boost: a bot with the heal function keeps both, the others spend
            // Holy Shock on damage without the boost (it is worth more on a heal).
            if (!bot.HealsForTeam() && Cast(pVictim, s.pHolyShock))
                return true;

            if (IsUndeadOrDemon(pVictim) && Cast(pVictim, s.pExorcism))
                return true;

            // (fewer than 3 attackers: too much pushback)
            if (IsUndeadOrDemon(pVictim) && me->GetAttackers().size() < 3 && Cast(pVictim, s.pHolyWrath))
                return true;

            // (the chase: UpdateAI restarts an idle one before the rotation, or the Commander
            // melee stance module chases)
            return false;
        }
    };
}

std::unique_ptr<ClassRotation> ClassRotations::MakePaladin(TeamBotAI& bot)
{
    return std::make_unique<PaladinRotation>(bot);
}
