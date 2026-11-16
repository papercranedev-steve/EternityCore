/*
 * Paladin rotation (moved out of PartyBotAI.cpp 2026-11-14, same steps).
 *
 * Out of combat: aura, Righteous Fury (tank), blessings on the party, heals (healer).
 * Aura and blessings: the brain says which (Commander/Coordination/Duties/BlessingAssigner.h,
 * TeamPlan::blessings, user 2026-11-15); while that module is off, the same lists without the
 * other paladins (BlessingAssigner::Order / AuraOrder). Until 2026-11-15 one random blessing
 * for everyone and one random aura.
 *
 * In combat:
 *   1. Attacked: Holy Shield; Turn Evil on a second undead attacker (not a tank).
 *   2. Healer: heals (the Commander heal module while the party fights).
 *      Others: the judgement the brain gave it (Commander/Coordination/Duties/
 *      JudgementAssigner.h: Seal of Wisdom / Light, Judgement, then its own seal again; on a
 *      victim living JUDGEMENT_MIN_TTD_SEC+ without one of that kind), seal, Judgement, Hammer of Justice below 20% health, Hammer of Wrath on a victim
 *      below 20%, Consecration (not a tank), Holy Shock (+ Divine Favor), Exorcism on undead,
 *      Holy Wrath on undead / demons.
 *   3. Blessing of Freedom when rooted or slowed; a self heal below 30% (not a healer).
 * Blessing of Sacrifice is not cast (user 2026-11-15: it took a GCD before any heal, even in an
 * emergency, and moved the damage onto the paladin).
 *
 * The Commander decides: Divine Shield, Blessing of Protection, Lay on Hands (team rescue),
 * Hammer of Justice on a cast (InterruptBehavior), Consecration for a tank (ThreatBehavior,
 * ClassKit::AreaThreat; the rotation cast it on "3 attackers" too until 2026-11-14), dispels
 * in combat (DispelBehavior), a healer's Divine Favor and Holy Shock heals (heal module).
 * Still the rotation's own (no Commander module yet, doc 2026-11-14-hands-cleanup.md 3):
 * Turn Evil, Hammer of Justice at low health.
 */

#include "Rotations/ClassRotation.h"
#include "PartyBotAI.h"
#include "Player.h"
#include "Group.h"
#include "SpellMgr.h"
#include "SpellAuras.h"
#include "Commander/CommanderMgr.h"
#include "Commander/PartyCommander.h"
#include "Commander/Coordination/Duties/BlessingAssigner.h"
#include "Commander/Perception/WorldModel.h"
#include "Commander/Perception/Values/ValueModel.h"

namespace
{
    using Commander::BlessingKind;
    using Commander::PaladinAuraKind;

    class PaladinRotation : public ClassRotation
    {
    public:
        using ClassRotation::ClassRotation;

        // A judgement is put on a victim expected to live at least this long (seal + Judgement:
        // two casts and their mana for a 10 s debuff the melee hits refresh).
        static constexpr float JUDGEMENT_MIN_TTD_SEC = 15.0f;

        void UpdateOutOfCombat() override
        {
            PartyBotAI& bot = m_bot;
            Player* me = bot.me;
            auto const& s = bot.m_spells.paladin;

            if (Cast(me, ChosenAura()))
                return;

            if (bot.m_role == ROLE_TANK && Cast(me, s.pRighteousFury))
                return;

            if (BlessParty())
                return;

            EndBuffing(m_lastBlessing);

            if (bot.m_role == ROLE_HEALER && bot.FindAndHealInjuredAlly())
                return;
        }

        void UpdateInCombat() override
        {
            PartyBotAI& bot = m_bot;
            Player* me = bot.me;
            auto const& s = bot.m_spells.paladin;

            // Made the tank in the fight (a stand-in: Commander CrisisAssigner): Righteous Fury
            // was only cast out of combat.
            if (bot.m_role == ROLE_TANK && Cast(me, s.pRighteousFury))
                return;

            if (!me->GetAttackers().empty())
            {
                if (Cast(me, s.pHolyShield))
                    return;

                if (s.pTurnEvil && bot.m_role != ROLE_TANK)
                {
                    Unit* pAttacker = bot.SelectAttackerDifferentFrom(me->GetVictim());
                    if (pAttacker && pAttacker->GetCreatureType() == CREATURE_TYPE_UNDEAD && Cast(pAttacker, s.pTurnEvil))
                        return;
                }
            }

            if (bot.GetRole() == ROLE_HEALER)
            {
                if (bot.FindAndHealInjuredAlly(80.0f, 90.0f))
                    return;
                if (bot.FindAndPreHealTarget())
                    return;
            }
            else if (Damage())
                return;

            if ((me->HasUnitState(UNIT_STATE_ROOT) || me->HasAuraType(SPELL_AURA_MOD_DECREASE_SPEED)) &&
                Cast(me, s.pBlessingOfFreedom))
                return;

            if (bot.GetRole() != ROLE_HEALER && me->GetHealthPercent() < 30.0f)
                bot.HealInjuredTarget(me);
        }

    private:
        SpellEntry const* m_lastBlessing = nullptr;     // EndBuffing

        SpellEntry const* KnownBlessing(BlessingKind kind) const
        {
            auto const& s = m_bot.m_spells.paladin;
            switch (kind)
            {
                case BlessingKind::Wisdom:    return s.pBlessingOfWisdom;
                case BlessingKind::Might:     return s.pBlessingOfMight;
                case BlessingKind::Kings:     return s.pBlessingOfKings;
                case BlessingKind::Salvation: return s.pBlessingOfSalvation;
                case BlessingKind::Light:     return s.pBlessingOfLight;
                case BlessingKind::Sanctuary: return s.pBlessingOfSanctuary;
                default:                      return nullptr;
            }
        }

        SpellEntry const* KnownAura(PaladinAuraKind kind) const
        {
            auto const& s = m_bot.m_spells.paladin;
            switch (kind)
            {
                case PaladinAuraKind::Devotion:         return s.pDevotionAura;
                case PaladinAuraKind::Concentration:    return s.pConcentrationAura;
                case PaladinAuraKind::Retribution:      return s.pRetributionAura;
                case PaladinAuraKind::Sanctity:         return s.pSanctityAura;
                case PaladinAuraKind::ShadowResistance: return s.pShadowResistanceAura;
                case PaladinAuraKind::FrostResistance:  return s.pFrostResistanceAura;
                case PaladinAuraKind::FireResistance:   return s.pFireResistanceAura;
                default:                                return nullptr;
            }
        }

        // The brain's blessing plan, nullptr while the module (or the Commander) is off.
        std::shared_ptr<Commander::PartyCommander> PlanOwner() const
        {
            if (!sCommanderMgr.IsEnabled())
                return nullptr;
            std::shared_ptr<Commander::PartyCommander> commander = sCommanderMgr.FindCommander(m_bot.me);
            return commander && commander->GetPlan().blessings.active ? commander : nullptr;
        }

        SpellEntry const* ChosenAura() const
        {
            if (std::shared_ptr<Commander::PartyCommander> owner = PlanOwner())
            {
                uint32 const spellId = owner->GetPlan().blessings.AuraFor(m_bot.me->GetObjectGuid());
                return spellId ? sSpellMgr.GetSpellEntry(spellId) : nullptr;
            }
            for (PaladinAuraKind kind : Commander::BlessingAssigner::AuraOrder())
                if (SpellEntry const* spell = KnownAura(kind))
                    return spell;
            return nullptr;
        }

        // Module off: the first of the member's list that I know and nobody else gave it.
        SpellEntry const* OwnBlessingChoice(Player* pMember) const
        {
            uint8 role = ROLE_INVALID;
            if (PartyBotAI const* ai = dynamic_cast<PartyBotAI const*>(pMember->AI()))
                role = uint8(ai->m_role);
            ObjectGuid const self = m_bot.me->GetObjectGuid();
            for (BlessingKind kind : Commander::BlessingAssigner::Order(pMember->GetClass(), role))
            {
                SpellEntry const* spell = KnownBlessing(kind);
                if (!spell)
                    continue;
                bool fromOther = false;
                for (auto const& itr : pMember->GetSpellAuraHolderMap())
                    if (itr.second->GetCasterGuid() != self &&
                        Commander::PaladinBuffs::BlessingOf(itr.second->GetSpellProto()) == kind)
                        fromOther = true;
                if (!fromOther)
                    return spell;
            }
            return nullptr;
        }

        // One blessing cast (true) on the first member that misses the one it gets from me.
        // Greater Blessings first: one lands on the whole class and replaces my plain blessing
        // on a member of it that gets another one from me (a tank among warriors).
        bool BlessParty()
        {
            std::shared_ptr<Commander::PartyCommander> const owner = PlanOwner();
            return BlessParty(owner, true) || BlessParty(owner, false);
        }

        bool BlessParty(std::shared_ptr<Commander::PartyCommander> const& owner, bool greater)
        {
            PartyBotAI& bot = m_bot;
            Player* me = bot.me;
            Group* pGroup = me->GetGroup();
            if (!pGroup)
                return false;
            for (GroupReference* itr = pGroup->GetFirstMember(); itr != nullptr; itr = itr->next())
            {
                Player* pMember = itr->getSource();
                if (!pMember || !me->IsValidHelpfulTarget(pMember) || pMember->IsGameMaster() ||
                    !me->IsWithinLOSInMap(pMember) || !me->IsWithinDist(pMember, 30.0f))
                    continue;
                SpellEntry const* spell = nullptr;
                if (owner)
                {
                    if (uint32 const spellId = owner->GetPlan().blessings.BlessingFor(me->GetObjectGuid(), pMember->GetObjectGuid()))
                        spell = sSpellMgr.GetSpellEntry(spellId);
                }
                else
                    spell = OwnBlessingChoice(pMember);
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
            PartyBotAI& bot = m_bot;
            Player* me = bot.me;
            auto const& s = bot.m_spells.paladin;
            if (!pVictim || !s.pJudgement || !sCommanderMgr.IsEnabled())
                return JudgeStep::None;
            std::shared_ptr<Commander::PartyCommander> commander = sCommanderMgr.FindCommander(me);
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

        // True = cast.
        bool Damage()
        {
            PartyBotAI& bot = m_bot;
            Player* me = bot.me;
            auto const& s = bot.m_spells.paladin;

            Unit* pVictim = me->GetVictim();
            JudgeStep const kept = KeepJudgement(pVictim);
            if (kept == JudgeStep::Cast)
                return true;
            // Its seal is up for the judgement still to come: not replaced by the own one.
            bool const holdSeal = kept == JudgeStep::Waiting;

            bool const hasSeal = s.pSeal && me->HasAura(s.pSeal->Id);
            if (!holdSeal && !hasSeal && s.pSeal && bot.CanTryToCastSpell(me, s.pSeal))
                me->CastSpell(me, s.pSeal, false);

            if (!pVictim)
                return false;

            if (!holdSeal && hasSeal && me->GetPowerPercent(POWER_MANA) > 30.0f && Cast(pVictim, s.pJudgement))
                return true;

            if (me->GetHealthPercent() < 20.0f && !me->GetAttackers().empty() && Cast(pVictim, s.pHammerOfJustice))
                return true;

            if (pVictim->GetHealthPercent() < 20.0f && Cast(pVictim, s.pHammerOfWrath))
                return true;

            if (bot.m_role != ROLE_TANK && bot.GetAttackersInRangeCount(10.0f) > 2 && Cast(me, s.pConsecration))
                return true;

            if (s.pHolyShock && bot.CanTryToCastSpell(pVictim, s.pHolyShock))
            {
                if (s.pDivineFavor && bot.CanTryToCastSpell(me, s.pDivineFavor))
                    bot.DoCastSpell(me, s.pDivineFavor);
                if (bot.DoCastSpell(pVictim, s.pHolyShock) == SPELL_CAST_OK)
                    return true;
            }

            bool const undead = pVictim->IsCreature() && pVictim->GetCreatureType() == CREATURE_TYPE_UNDEAD;
            bool const demon = pVictim->IsCreature() && pVictim->GetCreatureType() == CREATURE_TYPE_DEMON;

            if (undead && Cast(pVictim, s.pExorcism))
                return true;

            // (fewer than 3 attackers: too much pushback)
            if ((undead || demon) && me->GetAttackers().size() < 3 && Cast(pVictim, s.pHolyWrath))
                return true;

            // (the chase: UpdateAI restarts an idle one before the rotation, or the Commander
            // melee stance module chases)
            return false;
        }
    };
}

std::unique_ptr<ClassRotation> ClassRotations::MakePaladin(PartyBotAI& bot)
{
    return std::make_unique<PaladinRotation>(bot);
}
