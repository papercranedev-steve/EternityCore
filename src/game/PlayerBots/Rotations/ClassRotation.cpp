#include "Rotations/ClassRotation.h"
#include "PartyBotAI.h"
#include "Player.h"
#include "MotionMaster.h"
#include "Spell.h"
#include "Pet.h"
#include "CreatureAI.h"

bool ClassRotation::Cast(Unit* pTarget, SpellEntry const* pSpell)
{
    return pSpell && pTarget && m_bot.CanTryToCastSpell(pTarget, pSpell) &&
           m_bot.DoCastSpell(pTarget, pSpell) == SPELL_CAST_OK;
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

bool ClassRotation::ShouldBeStealthed() const
{
    Player* me = m_bot.me;
    if (me->IsMounted())
        return false;
    if (me->GetVictim() || me->GetHealthPercent() < 10.0f)
        return true;
    if (Player* pLeader = m_bot.GetPartyLeader())
        return pLeader->IsDead() || pLeader->IsFeigningDeathSuccessfully() ||
               pLeader->HasAuraType(SPELL_AURA_MOD_STEALTH) ||
               pLeader->HasAuraType(SPELL_AURA_MOD_INVISIBILITY);
    return false;
}

bool ClassRotation::KeepStealth(SpellEntry const* pStealthSpell)
{
    if (!pStealthSpell || m_bot.IsStealthControlled())
        return false;

    bool const shouldStealth = ShouldBeStealthed();
    if (m_bot.me->HasAura(pStealthSpell->Id))
    {
        if (!shouldStealth)
            m_bot.me->RemoveAurasDueToSpellByCancel(pStealthSpell->Id);
        return false;
    }
    return shouldStealth && Cast(m_bot.me, pStealthSpell);
}

void ClassRotation::ShootWandBelowMana(Unit* pVictim, float manaPct)
{
    Player* me = m_bot.me;
    if (me->HasSpell(ClassRotations::SPELL_SHOOT_WAND) &&
       !me->IsMoving() &&
        me->GetPowerPercent(POWER_MANA) < manaPct &&
       !me->GetCurrentSpell(CURRENT_AUTOREPEAT_SPELL))
        me->CastSpell(pVictim, ClassRotations::SPELL_SHOOT_WAND, false);
}

void ClassRotation::ChaseWhenOutOfRange(Unit* pVictim)
{
    Player* me = m_bot.me;
    if (me->GetMotionMaster()->GetCurrentMovementGeneratorType() == IDLE_MOTION_TYPE &&
        me->GetDistance(pVictim) > 30.0f)
        m_bot.BeginChasing(pVictim);
}

void ClassRotation::SendPetAt(Unit* pVictim)
{
    Pet* pPet = m_bot.me->GetPet();
    if (!pPet || pPet->GetVictim())
        return;
    pPet->GetCharmInfo()->SetIsCommandAttack(true);
    pPet->AI()->AttackStart(pVictim);
}

namespace ClassRotations
{
    std::unique_ptr<ClassRotation> Create(PartyBotAI& bot)
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
