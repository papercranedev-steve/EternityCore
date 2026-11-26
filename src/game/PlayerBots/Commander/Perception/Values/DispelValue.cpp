#include "Commander/Perception/Values/DispelValue.h"
#include "Commander/Perception/WorldModel.h"
#include "Commander/Perception/Profiles/ProfileStore.h"
#include "Commander/Perception/Profiles/SpellProfile.h"
#include "Commander/Perception/Values/ValueModel.h"
#include "Commander/Perception/Values/CastValue.h"
#include "Commander/Perception/Values/ManaWorth.h"
#include "Commander/Perception/Spatial/MoveNeed.h"
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

    }

    Worth Of(WorldModel const& world, PartyMemberFacts const& member, DebuffFacts const& debuff,
             float immuneSec, bool orderedToMove)
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

        // Seconds it would still work: until it ends, or until the caster puts it back (not
        // before an immunity the removal leaves ends).
        float const effectSec = debuff.reapplySec > 0.0f
            ? std::min(leftSec, std::max(debuff.reapplySec, immuneSec)) : leftSec;

        bool periodic = false, manaLeech = false, slow = false, root = false, other = false;
        for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
        {
            if (spell->Effect[i] != SPELL_EFFECT_APPLY_AURA)
                continue;
            uint32 const aura = spell->EffectApplyAuraName[i];
            if (IsPeriodicDamage(aura))
                periodic = true;
            else if (aura == SPELL_AURA_PERIODIC_MANA_LEECH)
                manaLeech = true;
            else if (aura == SPELL_AURA_MOD_DECREASE_SPEED)
                slow = true;
            else if (aura == SPELL_AURA_MOD_ROOT)
                root = true;
            else
                other = true;
        }

        if (periodic || manaLeech)
        {
            SpellAmounts const amounts = SpellProfiles::Estimate(spell, debuff.casterLevel, 0.0f);
            float const durationSec = std::max(1.0f, profile->durationMs / 1000.0f);
            if (periodic)
            {
                float const perSec = amounts.periodicDamage * debuff.stacks / durationSec;
                w.points += Value::Points(world, perSec * effectSec);
            }
            if (manaLeech && member.manaPct >= 0.0f)
            {
                // The mana it still takes (no more than the member has), valued like an
                // interrupted mana drain (CastValues::DrainedManaPoints).
                float const perSec = amounts.mana * debuff.stacks / durationSec;
                float const mana = std::min(perSec * effectSec, float(member.mana));
                w.points += CastValues::DrainedManaPoints(world, member, mana);
                w.manaCritical = ManaWorth::Critical(world, member, mana);
            }
        }
        // A root or a slow costs only while the member has to move (MoveNeed; until 2026-11-26 a
        // root counted as "anything else" wherever the member stood).
        if ((root || slow) && (orderedToMove || MoveNeed::Of(world, member) != MoveReason::None))
            w.points += Value::LostAction(world, member, std::min(effectSec, root ? ROOT_SEC : SLOW_SEC));
        if (other)
            w.points += Value::LostAction(world, member, std::min(effectSec, OTHER_SEC));
        return w;
    }
}
}
