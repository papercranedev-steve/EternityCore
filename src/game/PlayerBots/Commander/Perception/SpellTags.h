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
    // What an enemy cast does, for interrupt priority. Higher value = interrupt first.
    enum class CastCategory : uint8
    {
        Other   = 0,
        Damage  = 1,
        Summon  = 2,
        Control = 3,    // fear, sleep, polymorph, charm, silence, stun
        Heal    = 4,
    };

    namespace SpellTags
    {
        CastCategory Categorize(SpellEntry const* spell);
        char const* CategoryName(CastCategory category);

        // Same condition Spell::EffectInterruptCast uses (SpellEffects.cpp ~3560):
        // silence-type prevention, and the cast is a non-instant generic cast with the
        // pushback interrupt flag, or a channel that action-cancels.
        bool IsInterruptible(SpellEntry const* spell, bool channeled, int32 castTimeMs);

        // Interrupts, silences and stuns a bot can use to stop a cast.
        bool StopsCasts(SpellEntry const* spell);
    }
}

#endif
