#include "Commander/VetoChecks.h"
#include "Commander/BotDirectives.h"
#include "Commander/CommanderMgr.h"
#include "Commander/Perception/ProfileStore.h"
#include "PartyBotAI.h"
#include "SpellAuras.h"
#include "SpellEntry.h"
#include "Timer.h"

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
        return sCommanderMgr.IsEnabled() &&
               bot->Directives().IsVetoed(VetoType::Interrupt, enemy->GetObjectGuid(), 0, WorldTimer::getMSTime());
    }

    bool IsHealVetoed(PartyBotAI const* bot, Unit const* target)
    {
        return sCommanderMgr.IsEnabled() &&
               bot->Directives().IsVetoed(VetoType::Heal, target->GetObjectGuid(), 0, WorldTimer::getMSTime());
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
