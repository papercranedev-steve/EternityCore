#include "Commander/Perception/CreatureProfile.h"
#include "Creature.h"
#include "CreatureEventAIMgr.h"
#include "ObjectMgr.h"
#include "SpellMgr.h"
#include "SpellEntry.h"
#include <algorithm>

namespace Commander
{
    CreatureSpellRef const* CreatureProfile::FindSpell(uint32 spellId) const
    {
        for (CreatureSpellRef const& s : spells)
            if (s.spellId == spellId)
                return &s;
        return nullptr;
    }

    bool CreatureProfile::Reapplies(uint32 spellId) const
    {
        for (CreatureSpellRef const& s : spells)
            if (s.spellId == spellId && s.reapplies)
                return true;
        return false;
    }

namespace CreatureProfiles
{
    namespace
    {
        void Add(CreatureProfile& p, CreatureSpellRef const& ref)
        {
            if (!ref.spellId || !sSpellMgr.GetSpellEntry(ref.spellId))
                return;
            // Keep one entry per spell and source; the first one found wins.
            for (CreatureSpellRef const& s : p.spells)
                if (s.spellId == ref.spellId && s.source == ref.source)
                    return;
            p.spells.push_back(ref);
        }

        void AddSpellList(CreatureProfile& p, uint32 listId)
        {
            CreatureSpellsList const* list = listId ? sObjectMgr.GetCreatureSpellsList(listId) : nullptr;
            if (!list)
                return;
            for (CreatureSpellsEntry const& e : *list)
            {
                CreatureSpellRef ref;
                ref.spellId = e.spellId;
                ref.source = SpellSource::SpellList;
                ref.castTarget = e.castTarget;
                ref.repeatMinMs = e.delayRepeatMin;
                ref.repeatMaxMs = e.delayRepeatMax;
                ref.reapplies = e.delayRepeatMax > 0 && e.delayRepeatMax <= REAPPLY_REPEAT_MAX_MS;
                Add(p, ref);
            }
        }

        void AddOnHitProcs(CreatureProfile& p, uint32 const* auras)
        {
            if (!auras)
                return;
            for (uint32 const* aura = auras; *aura; ++aura)
            {
                SpellEntry const* auraSpell = sSpellMgr.GetSpellEntry(*aura);
                if (!auraSpell)
                    continue;
                for (uint32 i = 0; i < MAX_SPELL_EFFECTS; ++i)
                {
                    if (auraSpell->EffectApplyAuraName[i] != SPELL_AURA_PROC_TRIGGER_SPELL || !auraSpell->EffectTriggerSpell[i])
                        continue;
                    CreatureSpellRef ref;
                    ref.spellId = auraSpell->EffectTriggerSpell[i];
                    ref.source = SpellSource::OnHit;
                    ref.viaAuraId = auraSpell->Id;
                    ref.procChance = auraSpell->procChance;
                    ref.reapplies = true;
                    Add(p, ref);
                }
            }
        }

        void AddEventAI(CreatureProfile& p)
        {
            auto const& events = sEventAIMgr.GetCreatureEventAIMap();
            auto itr = events.find(p.entry);
            if (itr == events.end())
                return;

            for (CreatureEventAI_Event const& ev : itr->second)
            {
                for (uint32 a = 0; a < MAX_ACTIONS; ++a)
                {
                    ScriptMap const* actions = ev.action[a];
                    if (!actions)
                        continue;
                    for (auto const& step : *actions)
                    {
                        ScriptInfo const& info = step.second;
                        if (info.command == SCRIPT_COMMAND_FLEE && ev.event_type == EVENT_T_HP)
                            p.fleeAtHealthPct = std::max(p.fleeAtHealthPct, float(ev.percent_range.percentMax));

                        if (info.command != SCRIPT_COMMAND_CAST_SPELL)
                            continue;
                        CreatureSpellRef ref;
                        ref.spellId = info.castSpell.spellId;
                        ref.source = SpellSource::EventAI;
                        if (ev.event_type == EVENT_T_TIMER_IN_COMBAT)
                        {
                            ref.repeatMinMs = ev.timer.repeatMin;
                            ref.repeatMaxMs = ev.timer.repeatMax;
                            ref.reapplies = (ev.event_flags & EFLAG_REPEATABLE) && ev.timer.repeatMax > 0 &&
                                            ev.timer.repeatMax <= REAPPLY_REPEAT_MAX_MS;
                        }
                        Add(p, ref);
                    }
                }
            }
        }

        float MeleeAverage(CreatureProfile const& p, uint32 level)
        {
            CreatureClassLevelStats const* cls = sObjectMgr.GetCreatureClassLevelStats(p.unitClass ? p.unitClass : 1, std::max(1u, level));
            float const base = cls ? cls->melee_damage : 1.0f;
            return base * p.damageMultiplier * Creature::_GetDamageMod(int32(p.rank));
        }
    }

    CreatureProfile Build(CreatureInfo const* info)
    {
        CreatureProfile p;
        if (!info)
            return p;

        p.entry = info->entry;
        p.name = info->name;
        p.rank = info->rank;
        p.levelMin = info->level_min;
        p.levelMax = info->level_max;
        p.unitClass = info->unit_class;
        p.damageMultiplier = info->damage_multiplier;
        p.baseAttackTimeMs = info->base_attack_time ? info->base_attack_time : 2000;
        p.tauntImmune = (info->immunity_flags & CREATURE_IMMUNITY_TAUNT) != 0;
        p.mechanicImmuneMask = info->mechanic_immune_mask;

        AddSpellList(p, info->spell_list_id);
        AddOnHitProcs(p, info->auras);
        AddEventAI(p);
        return p;
    }

    float EstimateMeleeHit(CreatureProfile const& profile, uint32 level)
    {
        return MeleeAverage(profile, level);
    }

    float EstimateMeleeDps(CreatureProfile const& profile, uint32 level)
    {
        return MeleeAverage(profile, level) / (profile.baseAttackTimeMs / 1000.0f);
    }

    char const* SourceName(SpellSource source)
    {
        switch (source)
        {
            case SpellSource::SpellList: return "list";
            case SpellSource::EventAI:   return "eai";
            case SpellSource::OnHit:     return "on-hit";
        }
        return "?";
    }
}
}
