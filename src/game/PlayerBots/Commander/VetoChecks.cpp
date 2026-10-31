#include "Commander/VetoChecks.h"
#include "Commander/BotDirectives.h"
#include "Commander/CommanderMgr.h"
#include "Commander/Perception/ProfileStore.h"
#include "Commander/Perception/SpellTags.h"
#include "Commander/Behaviors/ThreatBehavior.h"
#include "Commander/Behaviors/ManaBudgetBehavior.h"
#include "PartyBotAI.h"
#include "Creature.h"
#include "SpellAuras.h"
#include "SpellEntry.h"
#include "Timer.h"
#include <algorithm>

namespace Commander
{
namespace VetoChecks
{
    namespace
    {
        uint32 DispelMaskOf(SpellEntry const* spell)
        {
            uint32 mask = 0;
            for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
                if (spell->Effect[i] == SPELL_EFFECT_DISPEL)
                    mask |= Spells::GetDispellMask(DispelType(spell->EffectMiscValue[i]));
            return mask;
        }

        // The spells the class rotations use behind IsInterruptVetoed (PartyBotAI.cpp:
        // paladin, shaman, mage, priest, warrior, rogue blocks).
        std::vector<SpellEntry const*> ReflexInterruptSpells(PartyBotAI const* bot)
        {
            auto const& s = bot->m_spells;
            switch (bot->me->GetClass())
            {
                case CLASS_PALADIN: return { s.paladin.pHammerOfJustice };
                case CLASS_SHAMAN:  return { s.shaman.pEarthShock };
                case CLASS_MAGE:    return { s.mage.pCounterspell };
                case CLASS_PRIEST:  return { s.priest.pSilence };
                case CLASS_WARRIOR: return { s.warrior.pPummel, s.warrior.pShieldBash };
                case CLASS_ROGUE:   return { s.rogue.pGouge, s.rogue.pKick };
                default:            return {};
            }
        }

        // Live immunity (auras such as Divine Shield included) plus the creature_template
        // mechanic / school immunities of every effect that stops the cast.
        bool CannotStop(Unit const* enemy, SpellEntry const* spell)
        {
            if (enemy->IsImmuneToSpell(spell, false))
                return true;
            if (Creature const* creature = enemy->ToCreature())
                return SpellTags::IsStopImmune(spell, creature->GetCreatureInfo()->mechanic_immune_mask,
                                               creature->GetCreatureInfo()->school_immune_mask);
            return false;
        }

        // Hammer of Justice on a stun-immune boss: the cast "succeeds", the stun is resisted on
        // hit, the cooldown is spent and the cast completes (scenario 7 problem 4, scenario 10).
        bool ImmuneToAllReflexInterrupts(PartyBotAI const* bot, Unit const* enemy)
        {
            bool any = false;
            for (SpellEntry const* spell : ReflexInterruptSpells(bot))
            {
                if (!spell)
                    continue;
                if (!CannotStop(enemy, spell))
                    return false;
                any = true;
            }
            return any;
        }
    }

    bool IsDispelVetoed(PartyBotAI const* bot, Unit const* target, SpellAuraHolder const* holder)
    {
        if (!sCommanderMgr.IsEnabled())
            return false;

        uint32 const spellId = holder->GetId();
        if (std::shared_ptr<SpellProfile const> profile = sProfileStore.GetSpell(spellId))
        {
            if (profile->hasOverride)
            {
                switch (profile->override.dispel)
                {
                    case DispelPolicy::Never:       return true;
                    case DispelPolicy::AfterCombat: return target->IsInCombat();
                    case DispelPolicy::Now:         return false;
                    case DispelPolicy::Auto:        break;
                }
            }
        }

        return bot->Directives().IsVetoed(VetoType::Dispel, target->GetObjectGuid(), spellId, WorldTimer::getMSTime());
    }

    bool IsInterruptVetoed(PartyBotAI const* bot, Unit const* enemy)
    {
        if (!sCommanderMgr.IsEnabled())
            return false;
        if (ImmuneToAllReflexInterrupts(bot, enemy))
            return true;
        return bot->Directives().IsVetoed(VetoType::Interrupt, enemy->GetObjectGuid(), 0, WorldTimer::getMSTime());
    }

    bool IsHealVetoed(PartyBotAI const* bot, Unit const* target)
    {
        // Fix: a heal veto lasts Arbiter::VETO_TICKS ticks (3 s by default), but every module
        // proposing one (ThreatBehavior, ManaBudgetBehavior, boss modules) only does so above
        // its emergency threshold. A target that drops below it while the veto still runs was
        // left unhealed for up to 3 s, and SelectHealTarget keeps picking it, so nobody else
        // was healed either. Below the lowest emergency threshold the veto no longer applies.
        float const threatFloor = ThreatBehavior::HEAL_IGNORE_THREAT_PCT;
        float const manaFloor = ManaBudgetBehavior::EMERGENCY_PCT;
        if (target->GetHealthPercent() < std::min(threatFloor, manaFloor))
            return false;
        return sCommanderMgr.IsEnabled() &&
               bot->Directives().IsVetoed(VetoType::Heal, target->GetObjectGuid(), 0, WorldTimer::getMSTime());
    }

    bool IsCharmedPartyMember(PartyBotAI const* bot, Unit const* target)
    {
        if (!sCommanderMgr.IsEnabled() || !target || !target->IsPlayer() || !target->IsCharmed())
            return false;
        if (target->GetCharmerGuid().IsPlayer())
            return false;   // a player's mind control (duels, PvP): not our business
        Player const* member = static_cast<Player const*>(target);
        return member != bot->me && member->IsInSameRaidWith(bot->me);
    }

    bool HasAllowedDebuff(PartyBotAI const* bot, Unit const* target, SpellEntry const* dispelSpell)
    {
        uint32 const mask = DispelMaskOf(dispelSpell);
        for (auto const& itr : target->GetSpellAuraHolderMap())
        {
            SpellAuraHolder const* holder = itr.second;
            if (holder->IsPositive() || !((1 << holder->GetSpellProto()->Dispel) & mask))
                continue;
            if (!IsDispelVetoed(bot, target, holder))
                return true;
        }
        return false;
    }
}
}
