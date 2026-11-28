#include "Commander/Perception/Profiles/CreatureProfile.h"
#include "Commander/Perception/Profiles/BossSheets.h"
#include "Creature.h"
#include "CreatureEventAIMgr.h"
#include "ScriptMgr.h"
#include "ObjectMgr.h"
#include "SpellMgr.h"
#include "SpellEntry.h"
#include "DBCStores.h"
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
                ref.reapplies = RepeatsSoon(e.delayRepeatMin, e.delayRepeatMax);
                ref.castFlags = e.castFlags;
                ref.targetParam1 = e.targetParam1;
                ref.targetParam2 = e.targetParam2;
                ref.initialMinMs = e.delayInitialMin;
                ref.initialMaxMs = e.delayInitialMax;
                if ((e.castFlags & CF_MAIN_RANGED_SPELL) && !p.mainRangedSpellId)
                    p.mainRangedSpellId = e.spellId;
                Add(p, ref);
            }
        }

        // A C++ boss script's own timers, written down by hand (BossSheets.h).
        void AddSheet(CreatureProfile& p, BossSheet const& sheet)
        {
            for (BossAbility const& a : sheet.abilities)
            {
                CreatureSpellRef ref;
                ref.spellId = a.spellId;
                ref.source = SpellSource::Sheet;
                ref.castTarget = a.castTarget;
                ref.initialMinMs = a.initialMinMs;
                ref.initialMaxMs = std::max(a.initialMinMs, a.initialMaxMs);
                ref.repeatMinMs = a.repeatMinMs;
                ref.repeatMaxMs = std::max(a.repeatMinMs, a.repeatMaxMs);
                ref.reapplies = RepeatsSoon(ref.repeatMinMs, ref.repeatMaxMs);
                ref.healthPct = a.belowPct;
                ref.sheetFlags = a.flags;
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

    }

    uint32 SpellSummonCount(uint32 spellId, uint32& entry, uint32& lifeMs)
    {
        {
            SpellEntry const* spell = sSpellMgr.GetSpellEntry(spellId);
            if (!spell)
                return 0;
            if (!lifeMs && spell->GetDuration() > 0)
                lifeMs = uint32(spell->GetDuration());
            uint32 count = 0;
            for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
            {
                switch (spell->Effect[i])
                {
                    case SPELL_EFFECT_SUMMON:
                    case SPELL_EFFECT_SUMMON_WILD:
                    case SPELL_EFFECT_SUMMON_GUARDIAN:
                    case SPELL_EFFECT_SUMMON_DEMON:
                        if (!entry)
                            entry = uint32(spell->EffectMiscValue[i]);
                        count += uint32(std::max(1, spell->CalculateSimpleValue(SpellEffectIndex(i))));
                        break;
                    default:
                        break;
                }
            }
            return count;
        }
    }

    SpawnOn SpawnsOf(CreatureEventAI_Event const& ev)
    {
        {
            SpawnOn s;
            for (uint32 a = 0; a < MAX_ACTIONS; ++a)
            {
                ScriptMap const* actions = ev.action[a];
                if (!actions)
                    continue;
                for (auto const& step : *actions)
                {
                    ScriptInfo const& info = step.second;
                    if (info.command == SCRIPT_COMMAND_TEMP_SUMMON_CREATURE)
                    {
                        if (!s.creatureEntry)
                            s.creatureEntry = info.summonCreature.creatureEntry;
                        if (!s.lifeMs)
                            s.lifeMs = info.summonCreature.despawnDelay;
                        ++s.count;
                    }
                    else if (info.command == SCRIPT_COMMAND_CAST_SPELL)
                        s.count += SpellSummonCount(info.castSpell.spellId, s.creatureEntry, s.lifeMs);
                }
            }
            return s;
        }
    }

    namespace
    {
        // Kills its caster and damages around it (SelfDestruct). Average damage and radius.
        bool IsSelfDestruct(uint32 spellId, float& radius, float& damage, bool& physical)
        {
            SpellEntry const* spell = sSpellMgr.GetSpellEntry(spellId);
            if (!spell)
                return false;
            bool killsSelf = false;
            damage = 0.0f;
            radius = 0.0f;
            for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
            {
                if (spell->Effect[i] == SPELL_EFFECT_INSTAKILL && spell->EffectImplicitTargetA[i] == TARGET_UNIT_CASTER)
                    killsSelf = true;
                else if (spell->Effect[i] == SPELL_EFFECT_SCHOOL_DAMAGE && spell->EffectRadiusIndex[i])
                {
                    damage += float(spell->CalculateSimpleValue(SpellEffectIndex(i))) +
                              float(std::max(0, spell->EffectDieSides[i] - 1)) / 2.0f;
                    if (SpellRadiusEntry const* r = sSpellRadiusStore.LookupEntry(spell->EffectRadiusIndex[i]))
                        radius = std::max(radius, r->Radius);
                }
            }
            physical = spell->School == SPELL_SCHOOL_NORMAL;
            return killsSelf && damage > 0.0f && radius > 0.0f;
        }

        void AddSummonTimer(CreatureProfile& p, CreatureEventAI_Event const& ev)
        {
            SummonTimer t;
            for (uint32 a = 0; a < MAX_ACTIONS; ++a)
            {
                ScriptMap const* actions = ev.action[a];
                if (!actions)
                    continue;
                for (auto const& step : *actions)
                {
                    ScriptInfo const& info = step.second;
                    if (info.command == SCRIPT_COMMAND_TEMP_SUMMON_CREATURE)
                    {
                        if (!t.creatureEntry)
                            t.creatureEntry = info.summonCreature.creatureEntry;
                        if (!t.lifeMs)
                            t.lifeMs = info.summonCreature.despawnDelay;
                        ++t.count;
                    }
                    else if (info.command == SCRIPT_COMMAND_CAST_SPELL)
                        t.count += SpellSummonCount(info.castSpell.spellId, t.creatureEntry, t.lifeMs);
                }
            }
            if (!t.count)
                return;
            t.initialMinMs = ev.timer.initialMin;
            t.initialMaxMs = std::max(ev.timer.initialMin, ev.timer.initialMax);
            if (ev.event_flags & EFLAG_REPEATABLE)
            {
                t.repeatMinMs = ev.timer.repeatMin;
                t.repeatMaxMs = std::max(ev.timer.repeatMin, ev.timer.repeatMax);
            }
            p.summonTimers.push_back(t);
        }

        void AddEventAI(CreatureProfile& p)
        {
            auto const& events = sEventAIMgr.GetCreatureEventAIMap();
            auto itr = events.find(p.entry);
            if (itr == events.end())
                return;

            for (CreatureEventAI_Event const& ev : itr->second)
            {
                if (ev.event_type == EVENT_T_TIMER_IN_COMBAT)
                    AddSummonTimer(p, ev);
                // On death / on aggro, unconditional only (see SpawnOn).
                if ((ev.event_type == EVENT_T_DEATH || ev.event_type == EVENT_T_AGGRO) && !ev.condition_id)
                {
                    SpawnOn const s = SpawnsOf(ev);
                    if (s.count && s.creatureEntry)
                        (ev.event_type == EVENT_T_DEATH ? p.deathSpawns : p.aggroSpawns).push_back(s);
                }

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
                        // (Setting instance data no longer makes it a boss: trash does it too,
                        // the boss list does it now, BossSheets.h.)

                        if (info.command != SCRIPT_COMMAND_CAST_SPELL)
                            continue;
                        CreatureSpellRef ref;
                        ref.spellId = info.castSpell.spellId;
                        ref.source = SpellSource::EventAI;
                        if (ev.event_type == EVENT_T_HP)
                            ref.healthPct = float(ev.percent_range.percentMax);
                        if (!p.selfDestruct.spellId)
                        {
                            SelfDestruct sd;
                            if (IsSelfDestruct(ref.spellId, sd.radius, sd.damage, sd.physical))
                            {
                                sd.spellId = ref.spellId;
                                if (ev.event_type == EVENT_T_TIMER_IN_COMBAT)
                                {
                                    sd.afterMinMs = ev.timer.initialMin;
                                    sd.afterMaxMs = std::max(ev.timer.initialMin, ev.timer.initialMax);
                                }
                                p.selfDestruct = sd;
                            }
                        }
                        if (ev.event_type == EVENT_T_TIMER_IN_COMBAT)
                        {
                            ref.repeatMinMs = ev.timer.repeatMin;
                            ref.repeatMaxMs = ev.timer.repeatMax;
                            ref.reapplies = (ev.event_flags & EFLAG_REPEATABLE) &&
                                            RepeatsSoon(ev.timer.repeatMin, ev.timer.repeatMax);
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
        p.healthMultiplier = info->health_multiplier;
        p.baseAttackTimeMs = info->base_attack_time ? info->base_attack_time : 2000;
        p.tauntImmune = (info->immunity_flags & CREATURE_IMMUNITY_TAUNT) != 0;
        p.mechanicImmuneMask = info->mechanic_immune_mask;
        p.schoolImmuneMask = info->school_immune_mask;
        p.noAggro = (info->flags_extra & CREATURE_FLAG_EXTRA_NO_AGGRO) != 0;

        std::string const scriptName = info->script_id ? sScriptMgr.GetScriptName(info->script_id) : "";
        p.scripted = !scriptName.empty();
        p.boss = info->rank == CREATURE_ELITE_WORLDBOSS || scriptName.compare(0, 5, "boss_") == 0 ||
                 BossSheets::IsListedBoss(info->entry);

        AddSpellList(p, info->spell_list_id);
        if (BossSheet const* sheet = BossSheets::Find(info->entry))
            AddSheet(p, *sheet);
        AddOnHitProcs(p, info->auras);
        if (info->auras)
            for (uint32 const* aura = info->auras; *aura; ++aura)
                if (SpellEntry const* s = sSpellMgr.GetSpellEntry(*aura))
                    for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
                        if (s->EffectApplyAuraName[i] == SPELL_AURA_MOD_STEALTH)
                            p.stealthy = true;
        // EventAI rows exist for many scripted creatures but only run with ai_name = EventAI
        // (CreatureAISelector); reading them for a C++ boss shows timers that never happen.
        if (!p.scripted && info->ai_name == "EventAI")
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

    float EstimateHealth(CreatureProfile const& profile, uint32 level)
    {
        CreatureClassLevelStats const* cls = sObjectMgr.GetCreatureClassLevelStats(profile.unitClass ? profile.unitClass : 1,
                                                                                    std::max(1u, level));
        if (!cls)
            return 1.0f;
        return std::max(1.0f, Creature::_GetHealthMod(int32(profile.rank)) * float(cls->health) * profile.healthMultiplier);
    }

    char const* SourceName(SpellSource source)
    {
        switch (source)
        {
            case SpellSource::SpellList: return "list";
            case SpellSource::EventAI:   return "eai";
            case SpellSource::OnHit:     return "on-hit";
            case SpellSource::Sheet:     return "sheet";
        }
        return "?";
    }
}
}
