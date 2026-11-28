#include "Commander/Perception/Spatial/HazardZones.h"
#include "Commander/Perception/Values/CastValue.h"
#include "Commander/Perception/Spatial/PackResolver.h"
#include "Commander/Perception/Profiles/ProfileStore.h"
#include "Commander/Perception/Profiles/BossSheets.h"
#include "Commander/Perception/Profiles/SpellTags.h"
#include "Commander/Perception/Values/ValueModel.h"
#include "Creature.h"            // CreatureInfo::auras
#include "DBCStores.h"
#include "ObjectMgr.h"
#include "ScriptMgr.h"          // ScriptCommands.h: TARGET_T_x (creature_spells castTarget)
#include "SharedDefines.h"
#include "SpellEntry.h"
#include "SpellMgr.h"
#include <algorithm>
#include <cmath>

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
            bool once = false;          // ability sheet: once a fight (gone once seen cast)
            bool everyone = false;      // ability sheet: BOSS_ABILITY_EVERYONE_LEAVES
            float arc = 0.0f;           // a cone (SpellTags::ConeArc), 0 = a circle
            bool atDest = false;        // lands at the cast's destination (DestAreaRadius)
            bool onGround = false;      // centred on a place (a boss module's fixed point)
            uint8 roles = 0;            // HazardRoles set by a boss module, 0 = derived
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

        // Largest radius of the effects that land at the cast's destination (header: At a
        // destination), 0 = none.
        float DestAreaRadius(SpellEntry const* spell)
        {
            float radius = 0.0f;
            for (uint32 i = 0; i < MAX_SPELL_EFFECTS; ++i)
            {
                if (!spell->Effect[i])
                    continue;
                uint32 const a = spell->EffectImplicitTargetA[i];
                uint32 const b = spell->EffectImplicitTargetB[i];
                bool const atDest = a == TARGET_ENUM_UNITS_ENEMY_AOE_AT_DEST_LOC || b == TARGET_ENUM_UNITS_ENEMY_AOE_AT_DEST_LOC ||
                                    a == TARGET_ENUM_UNITS_ENEMY_AOE_AT_DYNOBJ_LOC || b == TARGET_ENUM_UNITS_ENEMY_AOE_AT_DYNOBJ_LOC;
                if (!atDest)
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
            if ((ref.source != SpellSource::SpellList && ref.source != SpellSource::Sheet) || !IsSpreadTarget(ref.castTarget))
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
                    float coneRadius = 0.0f;
                    float const arc = SpellTags::ConeArc(spell, &coneRadius);
                    float const destRadius = radius > 0.0f ? 0.0f : DestAreaRadius(spell);
                    if (destRadius > 0.0f)
                    {
                        s.spellId = spell->Id;
                        s.hitSpellId = spell->Id;
                        s.kind = HazardKind::Burst;
                        s.rawRadius = destRadius;
                        s.atDest = true;
                    }
                    else if (radius > 0.0f)
                    {
                        s.spellId = spell->Id;
                        s.hitSpellId = spell->Id;
                        s.kind = HazardKind::Burst;
                        s.rawRadius = radius;
                    }
                    else if (arc != 0.0f && coneRadius > 0.0f)
                    {
                        s.spellId = spell->Id;
                        s.hitSpellId = spell->Id;
                        s.kind = HazardKind::Burst;
                        s.rawRadius = coneRadius;
                        s.arc = arc;
                    }
                    else if (!PulseOf(spell, true, s) && !RangeOf(spell, ref, s))
                        continue;
                    if (s.kind != HazardKind::Pulse)
                        s.intervalSec = RepeatSec(ref);
                    s.castTarget = ref.castTarget;
                    s.healthPct = ref.healthPct;
                    s.once = ref.OncePerFight();
                    s.everyone = (ref.sheetFlags & BOSS_ABILITY_EVERYONE_LEAVES) != 0;
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

        // The enemy has the mana for `spellId` (or is casting it already: paid at the end of
        // the cast). Spell::CalculatePowerCost without the per-level part (creature spells
        // in spell_template have manaCostPerlevel 0 or close to it).
        bool CanAfford(EnemyFacts const& e, uint32 spellId)
        {
            if (e.castingSpellId == spellId)
                return true;
            SpellEntry const* spell = sSpellMgr.GetSpellEntry(spellId);
            if (!spell || spell->powerType != POWER_MANA || e.maxMana == 0)
                return true;
            uint32 const cost = spell->manaCost + spell->ManaCostPercentage * e.createMana / 100;
            return e.mana >= cost;
        }

        bool MakeZone(WorldModel const& world, EnemyFacts const& e, HazardSource const& s, HazardZone& z)
        {
            bool const castingIt = s.kind != HazardKind::Pulse && e.castingSpellId == s.spellId;
            // A periodic trigger aura hurts while it is on the creature (a cast one like Herod's
            // Whirlwind comes and goes; a template one can be dispelled or run out).
            bool const pulseUp = s.kind == HazardKind::Pulse && e.HasPeriodicAura(s.spellId);
            if (s.kind == HazardKind::Pulse && !pulseUp)
                return false;
            // Not acting (immune, unselectable): only what it is doing right now (Doan's
            // Detonation in his bubble, a Whirlwind with an immunity on it).
            if (!e.acting && !castingIt && !pulseUp)
                return false;
            // Casts need a creature that can cast; periodic triggers tick regardless.
            if (s.kind != HazardKind::Pulse && (e.crowdControlled || e.silenced))
                return false;
            if (s.healthPct > 0.0f && e.healthPct > s.healthPct + CastValues::HP_EVENT_WINDOW_PCT)
                return false;
            // Once a fight and already cast.
            if (s.once && !castingIt && e.LastCastStartMs(s.spellId))
                return false;
            if (s.kind != HazardKind::Pulse && !CanAfford(e, s.spellId))
                return false;
            // A front cone at a random target: where it turns is only known while it is cast.
            if (s.arc > 0.0f && IsSpreadTarget(s.castTarget) && !castingIt)
                return false;
            // At a destination: only while it is read (a channel's area is a Ground zone).
            if (s.atDest && (!castingIt || !e.hasCastDest || e.castChanneled))
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
            z.center = s.atDest ? e.castDest : e.pos;
            z.onGround = s.atDest || s.onGround;
            z.repeatsHere = !s.atDest;
            z.radius = s.rawRadius + RADIUS_MARGIN;
            z.control = hp->controls && amounts.controlSec > 0.0f;
            z.breaksOnDamage = hp->breaksOnDamage;
            z.controlSec = amounts.controlSec;
            z.damage = amounts.TotalDamage();
            z.periodic = amounts.periodicDamage > amounts.damage;
            z.intervalSec = std::max(0.5f, s.intervalSec);
            z.casting = castingIt;
            z.castRemainingSec = z.casting ? e.castRemainingMs / 1000.0f : 0.0f;
            z.arc = s.arc;
            z.facing = e.facing;
            if (s.arc > 0.0f)
                z.aim = e.facingAt;

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
            // A cone: melee too (behind or beside it is out of a front cone, beside it out of a
            // back one; header).
            if (z.Cone() && (z.control || z.damage > 0.0f))
                z.roles |= HAZARD_HEALER | HAZARD_RANGED | HAZARD_MELEE;
            // The sheet says everyone gets out: melee always, the tank while it is being cast
            // (the caster stands still; at other times the boss would follow its tank).
            if (s.everyone && z.damage > 0.0f)
            {
                z.roles |= HAZARD_MELEE;
                if (z.casting)
                    z.roles |= HAZARD_TANK;
            }
            // At a destination: it stays where it lands, the enemy follows its tank out (header).
            if (s.atDest && z.damage > 0.0f)
                z.roles |= HAZARD_MELEE | HAZARD_TANK;
            // A boss module says who leaves.
            if (s.roles)
                z.roles = s.roles;
            return z.roles != 0;
        }

        // A Ground zone of a spell area on the ground (header: Ground).
        bool MakeGroundZone(GroundAreaFacts const& g, HazardZone& z)
        {
            SpellEntry const* spell = sSpellMgr.GetSpellEntry(g.spellId);
            std::shared_ptr<SpellProfile const> profile = sProfileStore.GetSpell(g.spellId);
            if (!spell || !profile || g.effIndex >= MAX_SPELL_EFFECTS)
                return false;
            uint32 const amplitudeMs = spell->EffectAmplitude[g.effIndex];
            int32 const durationMs = spell->GetDuration();
            if (!amplitudeMs || durationMs <= 0)
                return false;
            SpellAmounts const amounts = SpellProfiles::Estimate(spell, g.casterLevel, 0.0f);
            float const ticks = std::max(1.0f, float(durationMs) / float(amplitudeMs));

            z.enemy = g.caster;
            z.spellId = g.spellId;
            z.hitSpellId = g.spellId;
            z.kind = HazardKind::Ground;
            z.center = g.pos;
            z.onGround = true;
            z.radius = g.radius + RADIUS_MARGIN;
            // Per tick: the periodic part (the direct part of Flamestrike landed already).
            z.damage = amounts.periodicDamage / ticks;
            z.periodic = true;
            z.control = profile->controls && amounts.controlSec > 0.0f;
            z.breaksOnDamage = profile->breaksOnDamage;
            z.controlSec = amounts.controlSec;
            z.intervalSec = std::max(0.5f, amplitudeMs / 1000.0f);
            z.endsInSec = std::max(0.1f, g.msLeft / 1000.0f);

            z.roles = 0;
            if (z.control)
                z.roles |= HAZARD_HEALER | HAZARD_RANGED;
            if (z.damage > 0.0f)
                z.roles |= HAZARD_HEALER | HAZARD_RANGED | HAZARD_MELEE | HAZARD_TANK;
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
            case ROLE_TANK:      return HAZARD_TANK;
            default:             return 0;
        }
    }

    std::vector<HazardZone> Compute(WorldModel const& world)
    {
        std::vector<HazardZone> zones;
        for (EnemyFacts const& e : world.enemies)
        {
            // Feigning death, divine shield, evading: does nothing, except a cast or a periodic
            // aura it has going right now (MakeZone).
            if (!e.acting && !e.castingSpellId && e.periodicAuras.empty())
                continue;
            for (HazardSource const& s : SourcesOf(e.entry))
            {
                HazardZone z;
                if (MakeZone(world, e, s, z))
                    zones.push_back(z);
            }
        }
        // A boss module's (WorldModel::scriptedHazards): the same zone from its spell.
        for (ScriptedHazard const& h : world.scriptedHazards)
        {
            SpellEntry const* spell = sSpellMgr.GetSpellEntry(h.spellId);
            if (!spell)
                continue;
            EnemyFacts const* source = h.source.IsEmpty() ? nullptr : world.FindEnemy(h.source);
            if (source && !source->acting)
                continue;
            if (!source && !h.hasCenter)
                continue;
            EnemyFacts at;
            if (source)
                at = *source;
            else
            {
                at.guid = h.source;
                at.pos = h.center;
                at.level = h.level;
                at.acting = true;
            }
            HazardSource s;
            s.spellId = spell->Id;
            s.hitSpellId = spell->Id;
            s.kind = HazardKind::Burst;
            s.rawRadius = h.radius > 0.0f ? h.radius : CasterAreaRadius(spell);
            // (not caster-centred by its targets, a scripted self cast: its widest effect radius)
            for (uint32 i = 0; s.rawRadius <= 0.0f && i < MAX_SPELL_EFFECTS; ++i)
                if (SpellRadiusEntry const* r = sSpellRadiusStore.LookupEntry(spell->EffectRadiusIndex[i]))
                    s.rawRadius = std::max(s.rawRadius, r->Radius);
            s.intervalSec = h.intervalSec;
            s.onGround = !source;
            s.roles = h.roles;
            HazardZone z;
            if (s.rawRadius > 0.0f && MakeZone(world, at, s, z))
                zones.push_back(z);
        }
        // Spell areas on the ground.
        for (GroundAreaFacts const& g : world.groundAreas)
        {
            HazardZone z;
            if (MakeGroundZone(g, z))
                zones.push_back(z);
        }
        // Periodic areas of hostile creatures outside the fight (header: Pulse).
        for (BystanderAreaFacts const& b : world.bystanderAreas)
        {
            EnemyFacts at;
            at.guid = b.guid;
            at.pos = b.pos;
            at.level = b.level;
            for (BystanderAreaFacts::Aura const& a : b.auras)
                at.periodicAuras.push_back(a.spellId);
            for (BystanderAreaFacts::Aura const& a : b.auras)
            {
                SpellEntry const* aura = sSpellMgr.GetSpellEntry(a.spellId);
                HazardSource s;
                if (!aura || !PulseOf(aura, false, s))
                    continue;
                s.onGround = true;
                HazardZone z;
                if (!MakeZone(world, at, s, z))
                    continue;
                if (z.damage > 0.0f)
                    z.roles |= HAZARD_TANK;     // it stands where it is, the enemy follows its tank
                z.endsInSec = a.msLeft / 1000.0f;
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

    bool Covers(HazardZone const& zone, Position3 const& pos, float marginYd)
    {
        if (!zone.Cone())
            return DistanceTo(zone, pos) <= zone.radius - marginYd;
        return SpellTags::InCone(zone.center.x, zone.center.y, zone.center.z, zone.facing, zone.arc,
                                 zone.radius - marginYd, pos.x, pos.y, pos.z, marginYd);
    }

    float ExitYd(HazardZone const& zone, Position3 const& pos)
    {
        if (!Covers(zone, pos))
            return 0.0f;
        float const radial = std::max(0.0f, zone.radius - DistanceTo(zone, pos));
        if (!zone.Cone())
            return radial;
        float const dx = pos.x - zone.center.x;
        float const dy = pos.y - zone.center.y;
        float const d = std::sqrt(dx * dx + dy * dy);
        float const pi = float(M_PI);
        float const dir = zone.arc > 0.0f ? zone.facing : zone.facing + pi;
        float const diff = std::fabs(std::remainder(std::atan2(dy, dx) - dir, 2.0f * pi));
        float const half = std::fabs(zone.arc) * 0.5f;
        // Sideways to the arc's edge: perpendicular distance to that ray (beyond a right angle
        // the caster itself is the nearest edge point).
        float const off = half - diff;
        float const side = off >= pi * 0.5f ? d : d * std::sin(std::max(0.0f, off));
        return std::min(radial, side);
    }

    bool IsInside(std::vector<HazardZone> const& zones, uint8 roleBit, Position3 const& pos, ObjectGuid spare)
    {
        for (HazardZone const& z : zones)
            if (z.DangerousTo(roleBit) && (spare.IsEmpty() || z.aim != spare) && Covers(z, pos))
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
            case HazardKind::Ground: return "ground";
        }
        return "?";
    }
}
}
