/*
 * PartyBot Commander layer 1: spell facts derived from DBC data.
 * Pure functions on SpellEntry (static data), no game objects.
 */

#ifndef MANGOS_COMMANDER_SPELL_TAGS_H
#define MANGOS_COMMANDER_SPELL_TAGS_H

#include "Common.h"

class SpellEntry;

namespace Commander
{
    // What an enemy cast does, for interrupt priority. Higher value = interrupt first
    // when one spell has effects of several categories. Values are not stored anywhere.
    enum class CastCategory : uint8
    {
        Other     = 0,
        Damage    = 1,
        ManaDrain = 2,  // mana burn / drain: lost mana is lost healing
        Summon    = 3,  // summons creatures (adds, totems, guardians)
        Control   = 4,  // fear, sleep, polymorph, charm, silence, stun on someone else
        Heal      = 5,
    };

    namespace SpellTags
    {
        CastCategory Categorize(SpellEntry const* spell);
        char const* CategoryName(CastCategory category);

        // Effect `i` puts a control aura (fear, stun, charm, sleep, silence, ...) on the
        // spell's target. Auras on the caster itself do not count (Serpent Form silences
        // its own caster).
        bool IsControlEffect(SpellEntry const* spell, uint32 i);
        // The narrower part of IsControlEffect that takes the target out of the fight: it
        // cannot attack or cast (fear, confuse / polymorph, stun / sleep / sap / shackle /
        // banish, charm). Silence and pacify are left out: the target still fights.
        bool IsDisableEffect(SpellEntry const* spell, uint32 i);

        // Effect `i` summons creatures (EffectMiscValue = creature entry). Object and
        // critter summons are left out.
        bool IsCreatureSummonEffect(uint32 effect);

        // Same condition Spell::EffectInterruptCast uses (SpellEffects.cpp ~3560):
        // silence-type prevention, and the cast is a non-instant generic cast with the
        // pushback interrupt flag, or a channel that action-cancels.
        bool IsInterruptible(SpellEntry const* spell, bool channeled, int32 castTimeMs);

        // Interrupts, silences and stuns a bot can use to stop a cast.
        bool StopsCasts(SpellEntry const* spell);

        // Mechanics that stop the cast (1 << (MECHANIC_x - 1)): spell and effect mechanics.
        // Same bits as WorldModelBuilder fills into InterruptAbility::mechanicMask.
        uint32 StopMechanicMask(SpellEntry const* spell);

        // A target with these creature_template immunities is not stopped by `spell`:
        // every stopping mechanic is immune (stun on a stun-immune boss), or the spell's
        // school is immune. Same bit rules as Creature::IsImmuneToSpell / IsImmuneToSpellEffect.
        bool IsStopImmune(SpellEntry const* spell, uint32 mechanicImmuneMask, uint32 schoolImmuneMask);
    }
}

#endif
