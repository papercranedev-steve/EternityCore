/*
 * PartyBot Commander layer 1: cache of spell and creature profiles plus the
 * hand-written overrides (world tables custom_partybot_spell_override and
 * custom_partybot_creature_override, sql/custom/partybot_commander.sql).
 *
 * Profiles are built on first use and shared by all map threads (read-mostly,
 * guarded by a shared lock). Returned shared_ptrs stay valid after a reload.
 */

#ifndef MANGOS_COMMANDER_PROFILE_STORE_H
#define MANGOS_COMMANDER_PROFILE_STORE_H

#include "Commander/Perception/Profiles/SpellProfile.h"
#include "Commander/Perception/Profiles/CreatureProfile.h"
#include <memory>
#include <shared_mutex>
#include <string>
#include <unordered_map>

namespace Commander
{
    class ProfileStore
    {
    public:
        static ProfileStore& Instance();

        // nullptr if the spell / creature template does not exist.
        std::shared_ptr<SpellProfile const> GetSpell(uint32 spellId);
        std::shared_ptr<CreatureProfile const> GetCreature(uint32 entry);

        // Re-reads the override tables and drops every cached profile.
        // Returns a one-line summary for the GM command.
        std::string ReloadOverrides();

        struct Counts
        {
            size_t spells = 0;
            size_t creatures = 0;
            size_t spellOverrides = 0;
            size_t creatureOverrides = 0;
            bool spellTable = false;
            bool creatureTable = false;
        };
        Counts GetCounts();

    private:
        ProfileStore() = default;
        void LoadOverridesLocked();     // caller holds the unique lock

        std::shared_timed_mutex m_lock;
        bool m_overridesLoaded = false;
        bool m_spellTable = false;
        bool m_creatureTable = false;
        std::unordered_map<uint32, SpellOverride> m_spellOverrides;
        std::unordered_map<uint32, CreatureOverride> m_creatureOverrides;
        std::unordered_map<uint32, std::shared_ptr<SpellProfile const>> m_spells;
        std::unordered_map<uint32, std::shared_ptr<CreatureProfile const>> m_creatures;
    };
}

#define sProfileStore Commander::ProfileStore::Instance()

#endif
