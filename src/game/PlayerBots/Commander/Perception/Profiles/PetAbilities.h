/*
 * PartyBot Commander layer 1 (eyes): what a pet spell does, from its data (hunter pets, demons).
 * One kind per spell, shared by the pet's training (CombatBotBaseAI::TrainHunterPet), its facts
 * (WorldModelBuilder: survival, threat drops) and the pet module (ThreatBehavior::ProposePet).
 *
 * No pet has a real taunt in 1.12: Growl, Torment and Suffering only add threat
 * (SPELL_EFFECT_THREAT > 0). A spell whose threat rides on damage or a stun (Scorpid Poison,
 * the Intimidation stun) is not a threat spell.
 */

#ifndef MANGOS_COMMANDER_PET_ABILITIES_H
#define MANGOS_COMMANDER_PET_ABILITIES_H

#include <cstdint>
#include <vector>

class Pet;
class SpellEntry;

namespace Commander
{
    namespace PetAbilities
    {
        enum class Kind : uint8_t
        {
            Threat,         // only adds threat on its target (Growl, Torment, Suffering) or a taunt aura
            ThreatDrop,     // only lowers threat on its target (Cower, Soothing Kiss)
            Damage,         // damage on its target (Bite, Claw, Scorpid Poison, Screech, Lightning Breath)
            AreaDamage,     // damage around the pet (Thunderstomp)
            Survival,       // damage taken down on itself (Shell Shield)
            Charge,         // runs to its target (boar Charge)
            Speed,          // run speed up on itself (Dash, Dive)
            Other,          // anything else (Harass, Prowl, Furious Howl, the Intimidation stun)
        };

        Kind Of(SpellEntry const* spell);

        // Learned, active (not passive) spells of that kind (pets keep only the highest rank).
        void Learned(Pet const* pet, Kind kind, std::vector<SpellEntry const*>& out);
        // The first learned spell of that kind, null if none.
        SpellEntry const* First(Pet const* pet, Kind kind);
        // Its autocast is on.
        bool IsAutocast(Pet const* pet, SpellEntry const* spell);
    }
}

#endif
