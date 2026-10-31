#include "Commander/Perception/ProfileStore.h"
#include "Database/DatabaseEnv.h"
#include "ObjectMgr.h"
#include "SpellMgr.h"
#include "Log.h"
#include <mutex>
#include <sstream>

namespace Commander
{
    ProfileStore& ProfileStore::Instance()
    {
        static ProfileStore instance;
        return instance;
    }

    std::shared_ptr<SpellProfile const> ProfileStore::GetSpell(uint32 spellId)
    {
        {
            std::shared_lock<std::shared_timed_mutex> read(m_lock);
            if (m_overridesLoaded)
            {
                auto itr = m_spells.find(spellId);
                if (itr != m_spells.end())
                    return itr->second;
            }
        }

        SpellEntry const* spell = sSpellMgr.GetSpellEntry(spellId);
        if (!spell)
            return nullptr;

        std::unique_lock<std::shared_timed_mutex> write(m_lock);
        if (!m_overridesLoaded)
            LoadOverridesLocked();
        auto itr = m_spells.find(spellId);
        if (itr != m_spells.end())
            return itr->second;

        auto profile = std::make_shared<SpellProfile>(SpellProfiles::Build(spell));
        auto ov = m_spellOverrides.find(spellId);
        if (ov != m_spellOverrides.end())
        {
            profile->hasOverride = true;
            profile->override = ov->second;
        }
        m_spells[spellId] = profile;
        return profile;
    }

    std::shared_ptr<CreatureProfile const> ProfileStore::GetCreature(uint32 entry)
    {
        {
            std::shared_lock<std::shared_timed_mutex> read(m_lock);
            if (m_overridesLoaded)
            {
                auto itr = m_creatures.find(entry);
                if (itr != m_creatures.end())
                    return itr->second;
            }
        }

        CreatureInfo const* info = sObjectMgr.GetCreatureTemplate(entry);
        if (!info)
            return nullptr;

        std::unique_lock<std::shared_timed_mutex> write(m_lock);
        if (!m_overridesLoaded)
            LoadOverridesLocked();
        auto itr = m_creatures.find(entry);
        if (itr != m_creatures.end())
            return itr->second;

        auto profile = std::make_shared<CreatureProfile>(CreatureProfiles::Build(info));
        auto ov = m_creatureOverrides.find(entry);
        if (ov != m_creatureOverrides.end())
        {
            profile->hasOverride = true;
            profile->override = ov->second;
        }
        m_creatures[entry] = profile;
        return profile;
    }

    void ProfileStore::LoadOverridesLocked()
    {
        m_overridesLoaded = true;
        m_spellOverrides.clear();
        m_creatureOverrides.clear();
        m_spells.clear();
        m_creatures.clear();

        std::unique_ptr<QueryResult> exists(WorldDatabase.Query("SHOW TABLES LIKE 'custom_partybot_spell_override'"));
        m_spellTable = exists != nullptr;
        if (m_spellTable)
        {
            std::unique_ptr<QueryResult> result(WorldDatabase.Query(
                "SELECT `spell_id`, `interrupt_mult`, `dispel`, `comment` FROM `custom_partybot_spell_override`"));
            if (result)
            {
                do
                {
                    Field* fields = result->Fetch();
                    SpellOverride o;
                    o.interruptMult = std::max(0.0f, fields[1].GetFloat());
                    uint32 const dispel = fields[2].GetUInt32();
                    o.dispel = dispel <= uint32(DispelPolicy::AfterCombat) ? DispelPolicy(dispel) : DispelPolicy::Auto;
                    o.comment = fields[3].GetCppString();
                    m_spellOverrides[fields[0].GetUInt32()] = o;
                } while (result->NextRow());
            }
        }

        exists = WorldDatabase.Query("SHOW TABLES LIKE 'custom_partybot_creature_override'");
        m_creatureTable = exists != nullptr;
        if (m_creatureTable)
        {
            std::unique_ptr<QueryResult> result(WorldDatabase.Query(
                "SELECT `entry`, `interrupt_mult`, `taunt_mult`, `comment` FROM `custom_partybot_creature_override`"));
            if (result)
            {
                do
                {
                    Field* fields = result->Fetch();
                    CreatureOverride o;
                    o.interruptMult = std::max(0.0f, fields[1].GetFloat());
                    o.tauntMult = std::max(0.0f, fields[2].GetFloat());
                    o.comment = fields[3].GetCppString();
                    m_creatureOverrides[fields[0].GetUInt32()] = o;
                } while (result->NextRow());
            }
        }

        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[PartyBot Commander] Overrides: %u spells, %u creatures%s",
            uint32(m_spellOverrides.size()), uint32(m_creatureOverrides.size()),
            (m_spellTable && m_creatureTable) ? "" : " (override tables missing, see sql/custom/partybot_commander.sql)");
    }

    std::string ProfileStore::ReloadOverrides()
    {
        std::unique_lock<std::shared_timed_mutex> write(m_lock);
        LoadOverridesLocked();

        std::ostringstream ss;
        ss << "Overrides reloaded: " << m_spellOverrides.size() << " spells"
           << (m_spellTable ? "" : " (table missing)") << ", " << m_creatureOverrides.size() << " creatures"
           << (m_creatureTable ? "" : " (table missing)") << ". Profile cache cleared.";
        return ss.str();
    }

    ProfileStore::Counts ProfileStore::GetCounts()
    {
        std::shared_lock<std::shared_timed_mutex> read(m_lock);
        Counts c;
        c.spells = m_spells.size();
        c.creatures = m_creatures.size();
        c.spellOverrides = m_spellOverrides.size();
        c.creatureOverrides = m_creatureOverrides.size();
        c.spellTable = m_spellTable;
        c.creatureTable = m_creatureTable;
        return c;
    }
}
