#include "Commander/VetoChecks.h"
#include "Commander/BotDirectives.h"
#include "Commander/Perception/Profiles/ProfileStore.h"
#include "Commander/Behaviors/Combat/ThreatBehavior.h"
#include "Commander/Behaviors/Support/ManaBudgetBehavior.h"
#include "PartyBotAI.h"
#include "SpellAuras.h"
#include "SpellEntry.h"
#include "SpellMgr.h"
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
    }

    bool IsDispelVetoed(PartyBotAI const* bot, Unit const* target, SpellAuraHolder const* holder)
    {
        uint32 const spellId = holder->GetId();
        std::shared_ptr<SpellProfile const> profile = sProfileStore.GetSpell(spellId);
        if (profile && profile->hasOverride)
        {
            switch (profile->override.dispel)
            {
                case DispelPolicy::Never:       return true;
                case DispelPolicy::AfterCombat: return target->IsInCombat();
                case DispelPolicy::Now:         return false;
                case DispelPolicy::Auto:        break;
            }
        }

        // Someone is about to die and this bot's heal order waits for the heal step after the
        // dispels: only a control effect (the member cannot act) is worth the global cooldown
        // (strat-20261102-012937: three dispels of a slow on a full-health rogue while the tank died).
        // The rotation dispels only out of combat (in combat the dispel module does): a healer
        // not yet in combat during the party's fight.
        return bot->Directives().HasUrgentHealOrder(WorldTimer::getMSTime()) && !(profile && profile->controls);
    }

    bool IsHealVetoed(PartyBotAI const* bot, Unit const* target)
    {
        // Fix: a heal veto lasts Arbiter::VETO_MS (3 s), but every module
        // proposing one (ThreatBehavior, ManaBudgetBehavior, boss modules) only does so above
        // its emergency threshold. A target that drops below it while the veto still runs was
        // left unhealed for up to 3 s, and SelectHealTarget keeps picking it, so nobody else
        // was healed either. Below the lowest emergency threshold the veto no longer applies.
        float const threatFloor = ThreatBehavior::HEAL_IGNORE_THREAT_PCT;
        float const manaFloor = ManaBudgetBehavior::EMERGENCY_PCT;
        if (target->GetHealthPercent() < std::min(threatFloor, manaFloor))
            return false;
        return bot->Directives().IsVetoed(VetoType::Heal, target->GetObjectGuid(), 0, WorldTimer::getMSTime());
    }

    bool IsCharmedPartyMember(PartyBotAI const* bot, Unit const* target)
    {
        if (!target || !target->IsPlayer() || !target->IsCharmed())
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
