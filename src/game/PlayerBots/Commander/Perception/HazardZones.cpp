#include "Commander/Perception/HazardZones.h"
#include "Commander/Perception/CastValue.h"
#include "Commander/Perception/PackResolver.h"
#include "Commander/Perception/ProfileStore.h"
#include "Commander/Perception/ValueModel.h"
#include "Creature.h"            // CreatureInfo::auras
#include "DBCStores.h"
#include "ObjectMgr.h"
#include "ScriptMgr.h"          // ScriptCommands.h: TARGET_T_x (creature_spells castTarget)
#include "SharedDefines.h"
#include "SpellEntry.h"
#include "SpellMgr.h"
#include <algorithm>

namespace Commander
{
namespace HazardZones
{
    namespace
    {
        // Where a zone comes from, before the enemy's level, position and state are applied.
        struct HazardSource
        {
            uint32 spellId = 0;
            uint32 hitSpellId = 0;
            HazardKind kind = HazardKind::Burst;
            float rawRadius = 0.0f;     // spell radius, Range: max range
            float intervalSec = 0.0f;
            uint8 castTarget = 0xFF;
            float healthPct = 0.0f;     // EventAI health event threshold, 0 = none
        };

        // Effect targets that select "enemies around the caster's position". The source
        // location of an AT_SRC_LOC search is the caster (TARGET_LOCATION_CASTER_SRC, or the
        // default source when A sets none), see Spell.cpp FillAreaTargets / PUSH_SRC_CENTER.
        bool IsCasterCentred(uint32 a, uint32 b)
        {
            if (b == TARGET_ENUM_UNITS_ENEMY_AOE_AT_SRC_LOC)
                return a == TARGET_LOCATION_CASTER_SRC || a == TARGET_UNIT_CASTER || a == TARGET_NONE;
            return a == TARGET_ENUM_UNITS_ENEMY_AOE_AT_SRC_LOC && b == TARGET_NONE;
        }

        // Largest radius of the caster-centred effects, 0 = none.
        float CasterAreaRadius(SpellEntry const* spell)
        {
            float radius = 0.0f;
            for (uint32 i = 0; i < MAX_SPELL_EFFECTS; ++i)
            {
                if (!spell->Effect[i] || !IsCasterCentred(spell->EffectImplicitTargetA[i], spell->EffectImplicitTargetB[i]))
                    continue;
                if (SpellRadiusEntry const* r = sSpellRadiusStore.LookupEntry(spell->EffectRadiusIndex[i]))
                    radius = std::max(radius, r->Radius);
            }
            return radius;
        }

        // Periodic trigger aura whose triggered spell is caster-centred (Fire Shield).
        // `selfOnly`: the aura must be cast on the caster itself (spell list entries); template
        // auras are always applied to the creature, whatever their target field says
        // (11966 has TARGET_UNIT_FRIEND).
        bool PulseOf(SpellEntry const* aura, bool selfOnly, HazardSource& out)
        {
            for (uint32 i = 0; i < MAX_SPELL_EFFECTS; ++i)
            {
                if (aura->Effect[i] != SPELL_EFFECT_APPLY_AURA ||
                    aura->EffectApplyAuraName[i] != SPELL_AURA_PERIODIC_TRIGGER_SPELL ||
                    !aura->EffectTriggerSpell[i] || !aura->EffectAmplitude[i])
                    continue;
                if (selfOnly && aura->EffectImplicitTargetA[i] != TARGET_UNIT_CASTER)
                    continue;
                SpellEntry const* trigger = sSpellMgr.GetSpellEntry(aura->EffectTriggerSpell[i]);
                if (!trigger)
                    continue;
                float const radius = CasterAreaRadius(trigger);
                if (radius <= 0.0f)
                    continue;
                out.spellId = aura->Id;
                out.hitSpellId = trigger->Id;
                out.kind = HazardKind::Pulse;
                out.rawRadius = radius;
                out.intervalSec = aura->EffectAmplitude[i] / 1000.0f;
                return true;
            }
            return false;
        }

        // creature_spells target types that do not mean "the tank" (ScriptCommands.h TARGET_T_x).
        bool IsSpreadTarget(uint8 castTarget)
        {
            switch (castTarget)
            {
                case TARGET_T_HOSTILE_SECOND_AGGRO:
                case TARGET_T_HOSTILE_LAST_AGGRO:
                case TARGET_T_HOSTILE_RANDOM:
                case TARGET_T_HOSTILE_RANDOM_NOT_TOP:
                case TARGET_T_HOSTILE_FARTHEST:
                    return true;
                default:
                    return false;
            }
        }

        // Single target control at a random member: the spell range is the zone.
        bool RangeOf(SpellEntry const* spell, CreatureSpellRef const& ref, HazardSource& out)
        {
            if (ref.source != SpellSource::SpellList || !IsSpreadTarget(ref.castTarget))
                return false;
            std::shared_ptr<SpellProfile const> sp = sProfileStore.GetSpell(spell->Id);
            if (!sp || !sp->controls || sp->radius > 0.0f)
                return false;
            bool singleEnemy = false;
            for (uint32 i = 0; i < MAX_SPELL_EFFECTS; ++i)
                singleEnemy = singleEnemy || (spell->Effect[i] && spell->EffectImplicitTargetA[i] == TARGET_UNIT_ENEMY);
            SpellRangeEntry const* range = sSpellRangeStore.LookupEntry(spell->rangeIndex);
            if (!singleEnemy || !range || range->maxRange < RANGE_ZONE_MIN || range->maxRange > RANGE_ZONE_MAX)
                return false;
            out.spellId = spell->Id;
            out.hitSpellId = spell->Id;
            out.kind = HazardKind::Range;
            out.rawRadius = range->maxRange;
            return true;
        }

        float RepeatSec(CreatureSpellRef const& ref)
        {
            return ref.repeatMaxMs ? (ref.repeatMinMs + ref.repeatMaxMs) / 2000.0f : CastValues::CAST_INTERVAL_SEC;
        }

        void AddSource(std::vector<HazardSource>& out, HazardSource const& s)
        {
            for (HazardSource const& o : out)
                if (o.spellId == s.spellId && o.kind == s.kind)
                    return;
            out.push_back(s);
        }

        // Static part, per creature entry.
        std::vector<HazardSource> SourcesOf(uint32 entry)
        {
            std::vector<HazardSource> out;
            if (std::shared_ptr<CreatureProfile const> profile = sProfileStore.GetCreature(entry))
            {
                for (CreatureSpellRef const& ref : profile->spells)
                {
                    // On-hit procs land on the melee victim (the tank).
                    if (ref.source == SpellSource::OnHit)
                        continue;
                    SpellEntry const* spell = sSpellMgr.GetSpellEntry(ref.spellId);
                    if (!spell)
                        continue;
                    HazardSource s;
                    float const radius = CasterAreaRadius(spell);
                    if (radius > 0.0f)
                    {
                        s.spellId = spell->Id;
                        s.hitSpellId = spell->Id;
                        s.kind = HazardKind::Burst;
                        s.rawRadius = radius;
                    }
                    else if (!PulseOf(spell, true, s) && !RangeOf(spell, ref, s))
                        continue;
                    if (s.kind != HazardKind::Pulse)
                        s.intervalSec = RepeatSec(ref);
                    s.castTarget = ref.castTarget;
                    s.healthPct = ref.healthPct;
                    AddSource(out, s);
                }
            }
            if (CreatureInfo const* info = sObjectMgr.GetCreatureTemplate(entry))
            {
                if (info->auras)
                {
                    for (uint32 const* aura = info->auras; *aura; ++aura)
                    {
                        SpellEntry const* spell = sSpellMgr.GetSpellEntry(*aura);
                        HazardSource s;
                        if (spell && PulseOf(spell, false, s))
                            AddSource(out, s);
                    }
                }
            }
            return out;
        }

        // Members the spell can pick from (alive, not pets, within range of the caster).
        float RangeHitChance(WorldModel const& world, EnemyFacts const& e, HazardSource const& s)
        {
            uint32 count = 0;
            for (PartyMemberFacts const& m : world.party)
            {
                if (m.isPet || !m.alive || PackResolver::Distance(m.pos, e.pos) > s.rawRadius)
                    continue;
                if (s.castTarget == TARGET_T_HOSTILE_RANDOM_NOT_TOP && m.guid == e.victim)
                    continue;
                ++count;
            }
            return 1.0f / float(std::max(1u, count));
        }

        bool MakeZone(WorldModel const& world, EnemyFacts const& e, HazardSource const& s, HazardZone& z)
        {
            // Casts need a creature that can cast; periodic triggers tick regardless.
            if (s.kind != HazardKind::Pulse && (e.crowdControlled || e.silenced))
                return false;
            if (s.healthPct > 0.0f && e.healthPct > s.healthPct + CastValues::HP_EVENT_WINDOW_PCT)
                return false;

            SpellEntry const* hit = sSpellMgr.GetSpellEntry(s.hitSpellId);
            std::shared_ptr<SpellProfile const> hp = sProfileStore.GetSpell(s.hitSpellId);
            if (!hit || !hp)
                return false;
            SpellAmounts const amounts = SpellProfiles::Estimate(hit, e.level, e.meleeHit);

            z.enemy = e.guid;
            z.spellId = s.spellId;
            z.hitSpellId = s.hitSpellId;
            z.kind = s.kind;
            z.center = e.pos;
            z.radius = s.rawRadius + RADIUS_MARGIN;
            z.control = hp->controls && amounts.controlSec > 0.0f;
            z.breaksOnDamage = hp->breaksOnDamage;
            z.controlSec = amounts.controlSec;
            z.damage = amounts.TotalDamage();
            z.periodic = amounts.periodicDamage > amounts.damage;
            z.intervalSec = std::max(0.5f, s.intervalSec);
            z.casting = s.kind != HazardKind::Pulse && e.castingSpellId == s.spellId;
            z.castRemainingSec = z.casting ? e.castRemainingMs / 1000.0f : 0.0f;

            z.roles = 0;
            if (z.control)
                z.roles |= HAZARD_HEALER | HAZARD_RANGED;
            if (s.kind == HazardKind::Range)
            {
                if (!z.control)
                    return false;
                z.hitChance = RangeHitChance(world, e, s);
            }
            else if (z.damage > 0.0f)
            {
                z.roles |= HAZARD_HEALER | HAZARD_RANGED;
                if (s.kind == HazardKind::Pulse || z.periodic)
                    z.roles |= HAZARD_MELEE;
            }
            return z.roles != 0;
        }

        struct ZoneCache
        {
            WorldModel const* world = nullptr;
            uint32 version = 0;
            uint32 refreshedAtMs = 0;
            std::vector<HazardZone> zones;
        };

        // Same safety argument as the CastValues planning cache (CastValue.cpp): the model is
        // only written by PartyCommander::Refresh, which bumps version; one table per thread.
        thread_local ZoneCache t_zoneCache;
    }

    uint8 RoleBit(uint8 role)
    {
        switch (role)
        {
            case ROLE_HEALER:    return HAZARD_HEALER;
            case ROLE_RANGE_DPS: return HAZARD_RANGED;
            case ROLE_MELEE_DPS: return HAZARD_MELEE;
            default:             return 0;
        }
    }

    std::vector<HazardZone> Compute(WorldModel const& world)
    {
        std::vector<HazardZone> zones;
        for (EnemyFacts const& e : world.enemies)
        {
            // Feigning death, divine shield, evading: does nothing.
            if (!e.acting)
                continue;
            for (HazardSource const& s : SourcesOf(e.entry))
            {
                HazardZone z;
                if (MakeZone(world, e, s, z))
                    zones.push_back(z);
            }
        }
        return zones;
    }

    std::vector<HazardZone> const& Get(WorldModel const& world)
    {
        ZoneCache& cache = t_zoneCache;
        if (!world.version || cache.world != &world || cache.version != world.version ||
            cache.refreshedAtMs != world.refreshedAtMs)
        {
            cache.zones = Compute(world);
            cache.world = world.version ? &world : nullptr;  // version 0 = temporary, never reused
            cache.version = world.version;
            cache.refreshedAtMs = world.refreshedAtMs;
        }
        return cache.zones;
    }

    float HitPoints(WorldModel const& world, HazardZone const& zone, PartyMemberFacts const& member)
    {
        float points = Value::Points(world, zone.damage);
        if (zone.control)
        {
            float seconds = zone.controlSec;
            // Sleep on someone being hit lasts until the next hit (scenario 3).
            if (zone.breaksOnDamage && Value::IncomingOn(world, member.guid) > 0.0f)
                seconds = std::min(seconds, CastValues::WAKE_ON_DAMAGE_SEC);
            points += Value::LostAction(world, member, seconds);
        }
        return points;
    }

    float DistanceTo(HazardZone const& zone, Position3 const& pos)
    {
        return PackResolver::Distance(zone.center, pos);
    }

    bool IsInside(std::vector<HazardZone> const& zones, uint8 roleBit, Position3 const& pos)
    {
        for (HazardZone const& z : zones)
            if (z.DangerousTo(roleBit) && DistanceTo(z, pos) <= z.radius)
                return true;
        return false;
    }

    char const* KindName(HazardKind kind)
    {
        switch (kind)
        {
            case HazardKind::Burst: return "burst";
            case HazardKind::Pulse: return "pulse";
            case HazardKind::Range: return "range";
        }
        return "?";
    }
}
}
