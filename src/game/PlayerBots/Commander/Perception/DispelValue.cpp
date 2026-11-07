#include "Commander/Perception/DispelValue.h"
#include "Commander/Perception/WorldModel.h"
#include "Commander/Perception/ProfileStore.h"
#include "Commander/Perception/SpellProfile.h"
#include "Commander/Perception/ValueModel.h"
#include "SpellMgr.h"
#include "SpellEntry.h"
#include <algorithm>

namespace Commander
{
namespace DispelValue
{
    namespace
    {
        bool IsPeriodicDamage(uint32 aura)
        {
            return aura == SPELL_AURA_PERIODIC_DAMAGE || aura == SPELL_AURA_PERIODIC_LEECH ||
                   aura == SPELL_AURA_PERIODIC_DAMAGE_PERCENT;
        }

        // A healer or ranged member with a melee enemy on it has to walk away (escape module).
        bool NeedsToMove(WorldModel const& world, PartyMemberFacts const& m)
        {
            return (m.role == ROLE_HEALER || m.role == ROLE_RANGE_DPS) && Value::IncomingOn(world, m.guid) > 0.0f;
        }
    }

    Worth Of(WorldModel const& world, PartyMemberFacts const& member, DebuffFacts const& debuff)
    {
        Worth w;
        SpellEntry const* spell = sSpellMgr.GetSpellEntry(debuff.spellId);
        std::shared_ptr<SpellProfile const> profile = sProfileStore.GetSpell(debuff.spellId);
        if (!spell || !profile)
            return w;

        float const leftSec = debuff.remainingMs / 1000.0f;

        if (profile->hasOverride)
        {
            switch (profile->override.dispel)
            {
                case DispelPolicy::Never:
                case DispelPolicy::AfterCombat:
                    return w;
                case DispelPolicy::Now:
                    w.control = true;
                    w.points = Value::LostAction(world, member, std::max(leftSec, Value::GCD_SEC));
                    return w;
                case DispelPolicy::Auto:
                    break;
            }
        }

        if (profile->controls)
        {
            w.control = true;
            float sec = leftSec;
            if (profile->breaksOnDamage && Value::IncomingOn(world, member.guid) > 0.0f)
                sec = std::min(sec, WAKE_ON_DAMAGE_SEC);
            w.points = Value::LostAction(world, member, sec);
            return w;
        }

        // Seconds it would still work: until it ends, or until the caster puts it back.
        float const effectSec = debuff.reapplySec > 0.0f ? std::min(leftSec, debuff.reapplySec) : leftSec;

        bool periodic = false, slow = false, other = false;
        for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
        {
            if (spell->Effect[i] != SPELL_EFFECT_APPLY_AURA)
                continue;
            uint32 const aura = spell->EffectApplyAuraName[i];
            if (IsPeriodicDamage(aura))
                periodic = true;
            else if (aura == SPELL_AURA_MOD_DECREASE_SPEED)
                slow = true;
            else
                other = true;
        }

        if (periodic)
        {
            SpellAmounts const amounts = SpellProfiles::Estimate(spell, debuff.casterLevel, 0.0f);
            float const durationSec = std::max(1.0f, profile->durationMs / 1000.0f);
            float const perSec = amounts.periodicDamage * debuff.stacks / durationSec;
            w.points += Value::Points(world, perSec * effectSec);
        }
        if (slow && NeedsToMove(world, member))
            w.points += Value::LostAction(world, member, std::min(effectSec, SLOW_SEC));
        if (other)
            w.points += Value::LostAction(world, member, std::min(effectSec, OTHER_SEC));
        return w;
    }
}
}
