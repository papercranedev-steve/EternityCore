#include "Rotations/ClassRotation.h"
#include "TeamBotAI.h"
#include "Player.h"
#include "MotionMaster.h"
#include "Spell.h"
#include "Pet.h"
#include "CreatureAI.h"
#include "SpellMgr.h"
#include "Commander/Classes/ClassKit.h"
#include "Commander/CommanderMgr.h"
#include "Commander/PartyCommander.h"
#include "Commander/Perception/Values/DamageSpellValue.h"
#include "Timer.h"
#include <algorithm>
#include <vector>

bool ClassRotation::Cast(Unit* pTarget, SpellEntry const* pSpell)
{
    return pSpell && pTarget && !m_bot.IsHeldFromRotation(pSpell) && m_bot.CanTryToCastSpell(pTarget, pSpell) &&
           m_bot.DoCastSpell(pTarget, pSpell) == SPELL_CAST_OK;
}

bool ClassRotation::CastBestDamage(Unit* pVictim, std::initializer_list<DamageOption> options)
{
    if (!pVictim)
        return false;
    Player* me = m_bot.me;
    std::shared_ptr<Commander::PartyCommander> commander = sCommanderMgr.FindCommander(me);
    Commander::WorldModel const* world = commander ? &commander->GetWorld() : nullptr;
    if (!world || !world->FindEnemy(pVictim->GetObjectGuid()) || !world->FindMember(me->GetObjectGuid()))
    {
        for (DamageOption const& o : options)
            if (Cast(pVictim, o.spell))
                return true;
        return false;
    }

    struct Rated
    {
        SpellEntry const* spell;
        float perSec;
    };
    std::vector<Rated> rated;
    uint32 const nowMs = WorldTimer::getMSTime();
    for (DamageOption const& o : options)
    {
        if (!o.spell || m_bot.IsHeldFromRotation(o.spell) || !m_bot.CanTryToCastSpell(pVictim, o.spell))
            continue;
        float const perSec = Commander::DamageSpellValue::Rate(me, *world, pVictim, o.spell, nowMs, o.consumes).perSec;
        if (perSec > 0.0f)
            rated.push_back({ o.spell, perSec });
    }
    std::stable_sort(rated.begin(), rated.end(), [](Rated const& a, Rated const& b) { return a.perSec > b.perSec; });
    for (Rated const& r : rated)
        if (m_bot.DoCastSpell(pVictim, r.spell) == SPELL_CAST_OK)
            return true;
    return false;
}

bool ClassRotation::Buff(Unit* pTarget, SpellEntry const* pSpell)
{
    if (!Cast(pTarget, pSpell))
        return false;
    m_bot.m_isBuffing = true;
    m_bot.me->ClearTarget();
    return true;
}

void ClassRotation::EndBuffing(SpellEntry const* pBuffSpell)
{
    if (m_bot.m_isBuffing && (!pBuffSpell || !m_bot.me->HasGCD(pBuffSpell)))
        m_bot.m_isBuffing = false;
}

void ClassRotation::DropUnwantedStealth(SpellEntry const* pStealthSpell)
{
    Player* me = m_bot.me;
    if (!pStealthSpell || m_bot.IsStealthControlled() || me->GetVictim() || !me->HasAura(pStealthSpell->Id))
        return;
    me->RemoveAurasDueToSpellByCancel(pStealthSpell->Id);
}

void ClassRotation::ShootWandBelowMana(Unit* pVictim, float manaPct)
{
    Player* me = m_bot.me;
    // A healer's wand shot would pull its victim off the holder (TeamBotAI::IsDamagePullingAggro):
    // not started, and a running one stopped (the cast check only sees the start).
    if (SpellEntry const* pShoot = sSpellMgr.GetSpellEntry(ClassRotations::SPELL_SHOOT_WAND))
    {
        if (m_bot.IsDamagePullingAggro(pVictim, pShoot))
        {
            if (Spell const* pRepeat = me->GetCurrentSpell(CURRENT_AUTOREPEAT_SPELL))
                if (pRepeat->m_spellInfo->Id == ClassRotations::SPELL_SHOOT_WAND)
                    me->InterruptSpell(CURRENT_AUTOREPEAT_SPELL, true);
            return;
        }
    }
    // Learned and a wand equipped: without one the cast failed every tick.
    if (m_bot.CanShootWand() &&
       !me->IsMoving() &&
        me->GetPowerPercent(POWER_MANA) < manaPct &&
       !me->GetCurrentSpell(CURRENT_AUTOREPEAT_SPELL))
    {
        // Shoot checks the facing (SPELL_CUSTOM_FACE_TARGET): turn first.
        if (!me->IsFacingTarget(pVictim))
            m_bot.FaceTarget(pVictim);
        me->CastSpell(pVictim, ClassRotations::SPELL_SHOOT_WAND, false);
    }
}

void ClassRotation::ChaseWhenOutOfRange(Unit* pVictim)
{
    Player* me = m_bot.me;
    // The filler's range with the range talents (Arctic Reach: Frostbolt 36 yd), as the cast
    // check takes it; 30 yd without a filler. (Until 2026-11-26 always 30: a mage with Arctic
    // Reach walked in 6 yd closer than it needed.)
    SpellEntry const* pFiller = m_bot.Kit().Filler();
    float const range = pFiller ? CombatBotBaseAI::SpellMaxRange(me, pFiller) : 30.0f;
    if (me->GetMotionMaster()->GetCurrentMovementGeneratorType() == IDLE_MOTION_TYPE &&
        me->GetCombatDistance(pVictim) > range)
        m_bot.BeginChasing(pVictim);
}

bool ClassRotation::KeepPetAside()
{
    SpellEntry const* pAside = m_bot.Kit().PetStandAside();
    Pet* pPet = m_bot.me->GetPet();
    if (!pAside || !pPet || !pPet->IsAlive() || !pPet->GetCharmInfo() || !pPet->HasSpell(pAside->Id))
        return false;
    CharmInfo* pCharmInfo = pPet->GetCharmInfo();
    if (!pPet->HasReactState(REACT_PASSIVE))
        pPet->SetReactState(REACT_PASSIVE);
    if (pPet->GetVictim())
        pPet->HandlePetCommand(COMMAND_FOLLOW, nullptr);

    // Autocasts (Firebolt) would end the phase; party auras (Blood Pact) stay on and go up first.
    bool auraPending = false;
    std::vector<uint32> const autos = pPet->m_autospells;
    for (uint32 spellId : autos)
    {
        SpellEntry const* pSpell = sSpellMgr.GetSpellEntry(spellId);
        if (pSpell && pSpell->HasEffect(SPELL_EFFECT_APPLY_AREA_AURA_PARTY))
        {
            if (!pPet->HasAura(spellId))
                auraPending = true;
            continue;
        }
        pPet->ToggleAutocast(spellId, false);
        pCharmInfo->SetSpellAutocast(spellId, false);
    }
    if (!auraPending && !pPet->HasAura(pAside->Id) && !pPet->IsNonMeleeSpellCasted(false))
        pPet->CastSpell(pPet, pAside->Id, false);
    return true;
}

void ClassRotation::SendPetAt(Unit* pVictim)
{
    if (KeepPetAside())
        return;
    Pet* pPet = m_bot.me->GetPet();
    if (!pPet || pPet->GetVictim())
        return;
    pPet->GetCharmInfo()->SetIsCommandAttack(true);
    pPet->AI()->AttackStart(pVictim);
}

namespace ClassRotations
{
    std::unique_ptr<ClassRotation> Create(TeamBotAI& bot)
    {
        switch (bot.me->GetClass())
        {
            case CLASS_DRUID:   return MakeDruid(bot);
            case CLASS_HUNTER:  return MakeHunter(bot);
            case CLASS_MAGE:    return MakeMage(bot);
            case CLASS_PALADIN: return MakePaladin(bot);
            case CLASS_PRIEST:  return MakePriest(bot);
            case CLASS_ROGUE:   return MakeRogue(bot);
            case CLASS_SHAMAN:  return MakeShaman(bot);
            case CLASS_WARLOCK: return MakeWarlock(bot);
            case CLASS_WARRIOR: return bot.GetRole() == ROLE_TANK ? MakeWarriorTank(bot) : MakeWarrior(bot);
            default:            return nullptr;
        }
    }
}
