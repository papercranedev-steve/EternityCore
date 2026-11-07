#include "Commander/VetoChecks.h"
#include "Commander/BotDirectives.h"
#include "Commander/CommanderMgr.h"
#include "Commander/Classes/ClassKit.h"
#include "Commander/Perception/ProfileStore.h"
#include "Commander/Perception/SpellTags.h"
#include "Commander/Behaviors/ThreatBehavior.h"
#include "Commander/Behaviors/ManaBudgetBehavior.h"
#include "PartyBotAI.h"
#include "Creature.h"
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
            // The spells the class rotations use behind SkipsReflexInterrupt (ClassKit::ReflexInterrupts).
            for (SpellEntry const* spell : bot->Kit().ReflexInterrupts())
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
        // Only matters while the dispel module is off: with it on, the rotation does not
        // dispel in combat (Takeover::Dispel) and the arbiter weighs heal against dispel.
        return bot->Directives().HasUrgentHealOrder(WorldTimer::getMSTime()) && !(profile && profile->controls);
    }

    bool ImmuneToReflexInterrupts(PartyBotAI const* bot, Unit const* enemy)
    {
        return sCommanderMgr.IsEnabled() && ImmuneToAllReflexInterrupts(bot, enemy);
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

    bool IsGapCloserVetoed(PartyBotAI const* bot, Unit const* target, SpellEntry const* spell)
    {
        if (!spell || !target || !sCommanderMgr.IsEnabled() || !spell->HasEffect(SPELL_EFFECT_CHARGE))
            return false;
        return bot->Directives().IsVetoed(VetoType::GapCloser, target->GetObjectGuid(), 0, WorldTimer::getMSTime());
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
