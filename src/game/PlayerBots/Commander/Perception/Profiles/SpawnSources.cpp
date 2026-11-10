#include "Commander/Perception/Profiles/SpawnSources.h"
#include "Commander/Perception/Profiles/CreatureProfile.h"
#include "CreatureEventAI.h"
#include "CreatureEventAIMgr.h"
#include "GameObjectDefines.h"
#include "ObjectMgr.h"
#include "SpellEntry.h"
#include "SpellMgr.h"
#include <mutex>
#include <unordered_map>

namespace Commander
{
namespace SpawnSources
{
    namespace
    {
        std::unordered_map<uint32, uint8> g_sources;
        std::once_flag g_built;

        void AddSpellSummons(uint32 spellId, uint8 bit)
        {
            SpellEntry const* spell = sSpellMgr.GetSpellEntry(spellId);
            if (!spell)
                return;
            for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
            {
                switch (spell->Effect[i])
                {
                    case SPELL_EFFECT_SUMMON:
                    case SPELL_EFFECT_SUMMON_WILD:
                    case SPELL_EFFECT_SUMMON_GUARDIAN:
                    case SPELL_EFFECT_SUMMON_DEMON:
                        if (spell->EffectMiscValue[i] > 0)
                            g_sources[uint32(spell->EffectMiscValue[i])] |= bit;
                        break;
                    default:
                        break;
                }
            }
        }

        void Build()
        {
            for (auto const& itr : sObjectMgr.GetGameObjectInfoMap())
            {
                GameObjectInfo const* info = itr.second.get();
                if (info && info->type == GAMEOBJECT_TYPE_TRAP && info->trap.spellId)
                    AddSpellSummons(info->trap.spellId, FROM_TRAP);
            }
            for (auto const& itr : sEventAIMgr.GetCreatureEventAIMap())
            {
                for (CreatureEventAI_Event const& ev : itr.second)
                {
                    if ((ev.event_type != EVENT_T_DEATH && ev.event_type != EVENT_T_AGGRO) || ev.condition_id)
                        continue;
                    SpawnOn const s = CreatureProfiles::SpawnsOf(ev);
                    if (s.count && s.creatureEntry)
                        g_sources[s.creatureEntry] |= ev.event_type == EVENT_T_DEATH ? FROM_DEATH : FROM_AGGRO;
                }
            }
        }
    }

    uint8 Of(uint32 entry)
    {
        std::call_once(g_built, Build);
        auto itr = g_sources.find(entry);
        return itr != g_sources.end() ? itr->second : 0;
    }
}
}
