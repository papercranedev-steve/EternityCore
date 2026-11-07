/*
 * PartyBot Commander layer 1 (eyes): pet taunts (Growl, Torment, Suffering). Map thread, live
 * pet. Moved from ThreatBehavior (2026-11-07, team rescue) so the world model can say which
 * pets taunt by themselves (PartyMemberFacts::tauntsItself).
 */

#ifndef MANGOS_COMMANDER_PET_TAUNT_H
#define MANGOS_COMMANDER_PET_TAUNT_H

#include <vector>

class Pet;
class SpellEntry;

namespace Commander
{
    namespace PetTaunt
    {
        // A pet spell that makes enemies attack the pet: positive threat (Growl, Torment,
        // Suffering) or a taunt. Cower and Soothing Kiss lower threat (negative value): not taunts.
        bool IsTauntSpell(SpellEntry const* spell);
        // Taunt spells the pet has learned (pets keep only the highest rank).
        void Spells(Pet const* pet, std::vector<SpellEntry const*>& out);
        // The pet casts one of its taunts by itself (autocast on): it pulls enemies to it.
        bool HasAutocast(Pet const* pet);
    }
}

#endif
