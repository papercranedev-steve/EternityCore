/*
 * PartyBot Commander layer 1: areas around enemies that hurt or control party members who
 * stand in them. Read by Behaviors/Movement/PositioningBehavior ("position" module).
 * Design: .kiro/docs/2026-11-01-positioning.md. Scenarios: 04 (Deafening Screech), 05 (Fire
 * Shield, Fireball Volley), 09 (Psychic Scream), 13 (Melt Ore), 03 / 07 (control range).
 *
 * Derived on every world refresh from the WorldModel plus static data only (creature profiles,
 * creature_template.auras, spell_template, SpellRadius / SpellRange DBC). No live objects, so
 * Compute() can later run on recorded snapshots.
 *
 * Zone kinds
 *  Burst  - a spell of the creature's spell list / EventAI with an effect whose targets are
 *           "enemies around the caster": EffectImplicitTargetB = TARGET_ENUM_UNITS_ENEMY_AOE_AT_SRC_LOC
 *           with A = TARGET_LOCATION_CASTER_SRC / TARGET_UNIT_CASTER / none, or A = that enum
 *           with no B. The source location of such a spell is the caster's position
 *           (Spell.cpp FillAreaTargets PUSH_SRC_CENTER, 3D distance), so the area follows the
 *           caster. Checked in spell_template: Melt Ore 5159, Psychic Scream 13704, Fireball
 *           Volley 15285, Deafening Screech 3589, Fire Shield pulses 11968 / 11351 all use A = 22, B = 15.
 *           Cones (A = TARGET_ENUM_UNITS_ENEMY_IN_CONE_24 / _54, boss doc G4) are Burst zones
 *           with an arc (SpellTags::ConeArc: spell_cone, default 60 degrees; negative = behind,
 *           Tail Sweep) around the enemy's facing (EnemyFacts::facing: its cast target, else
 *           its victim). A front cone cast at a random target only while it is cast (where it
 *           turns is not known before). Dangerous to everyone but tanks (TankStanceBehavior
 *           turns the enemy instead), melee too; the unit it points at (HazardZone::aim)
 *           cannot leave it. Crystallize 16104 (Mother Smolderweb, 10 yd stun), breaths.
 *           At a destination (boss doc G2, 2026-11-28): an effect at TARGET_ENUM_UNITS_ENEMY_AOE_AT_DEST_LOC
 *           (16) or AT_DYNOBJ_LOC (28) of a spell with TARGET_FLAG_DEST_LOCATION: Flamestrike
 *           12468 (Jammal'an, 16 + a persistent fire at 28). Only while it is read, centred on
 *           EnemyFacts::castDest (SpellCaster::CastSpell gives it its target's position when it
 *           starts); before that where it lands is not known (random targets). Channels
 *           (Volley 22908) place their area at once: the Ground zone counts it.
 *           Not counted: script targets (7, creatures).
 *  Ground - a spell area on the ground (WorldModel::groundAreas: the dynamic object of a harmful
 *           persistent area aura, placed by something hostile): Flamestrike's fire after it
 *           lands, Volley, Blizzard. Its tick (the aura's periodic damage per amplitude) every
 *           amplitude until it ends (HazardZone::endsInSec).
 *  Pulse  - an aura on the creature itself (creature_template.auras, or a self-cast spell list
 *           entry) of type SPELL_AURA_PERIODIC_TRIGGER_SPELL whose triggered spell is a Burst
 *           shape: Fire Shield 11966 -> 11968 every 3 s, 5 yd (Fireguard), 11350 -> 11351
 *           (Blazing Fireguard, Fireguard Destroyer). Ticks while the aura is on the creature
 *           (EnemyFacts::periodicAuras, since 2026-11-28: a cast one like Herod's Whirlwind
 *           comes and goes, a template one can be dispelled or run out), also when it is
 *           controlled or silenced (periodic triggers ignore both).
 *  Range  - (second priority) single target control with a random / not-top target
 *           (creature_spells castTarget 2, 3, 4, 5, 7): Druid's Slumber 8040, 30 yd. Standing
 *           outside the spell range takes the member out of the random pick. Spell list and
 *           ability sheet entries only: EventAI has no castTarget.
 *
 * C++ scripted bosses: their ability sheet rows (Profiles/BossSheets.h) are profile spells like
 * a spell list. A sheet row once a fight is gone once it was seen cast.
 *
 * Burst and Range zones need a creature that can cast the spell now: not controlled, not
 * silenced, and with the mana for it (EnemyFacts::mana; a caster that ran dry chases in melee
 * and its Arcane Explosion area kept a mage walking away for 30 s, strat-20261101-224637).
 * An enemy that is not acting (immune, unselectable) only has the zones of what it does right
 * now: the spell it is casting, a periodic aura that is up (Doan's Detonation in his bubble).
 *
 * Who is in danger (roles bit mask)
 *  - control (silence, fear, stun, ... SpellProfile::controls): healers and ranged damage.
 *    Melee ignore control areas (a warrior does not mind a silence, and leaving melee costs more).
 *  - damage: healers and ranged; melee only for Pulse areas and Burst spells whose damage is
 *    mostly a periodic aura (Melt Ore: 7 fire per second for 20 s). Direct bursts such as
 *    Fireball Volley hit melee wherever they stand inside the 20 yd, nothing to gain.
 *  - tanks never: they hold the enemy where it is. Except an ability sheet row with
 *    BOSS_ABILITY_EVERYONE_LEAVES: melee always, the tank while it is being cast (the caster
 *    stands still for it).
 *  - areas on the ground and at a cast's destination (HazardZone::onGround) with damage:
 *    everyone, the tank too. The area stays where it is and the enemy follows its tank out.
 *    Control only: healers and ranged, as above.
 *  - a boss module zone with ScriptedHazard::roles: those roles.
 *
 * Value of one hit on a member (points, Value::Points): damage before resistances, plus for
 * control Value::LostAction(member, control seconds); control that breaks on damage on a member
 * being hit lasts CastValues::WAKE_ON_DAMAGE_SEC. Death penalties are left to the caller,
 * which knows how many hits it adds up.
 *
 * Boss modules add what the data does not show (WorldModel::scriptedHazards, IEncounter::AddFacts):
 * a Burst zone of a spell around an enemy or a fixed point (onGround), with the module's repeat
 * delay, dangerous to the roles derived from the spell or to the module's own (ScriptedHazard::roles).
 *
 * Known limits: the profile lists what a creature can cast, not what it will cast this fight
 * (EventAI health events count only near their threshold); C++ scripted bosses without an
 * ability sheet have no profile spells; the centre is the enemy position at the last refresh
 * (a moving enemy takes its area along between refreshes); a Pulse aura cast by a C++ script
 * counts only once it is up (no warning before it); resistances, spell
 * immunities and the spell leeway of moving casters are not modelled (RADIUS_MARGIN covers
 * the leeway in part).
 */

#ifndef MANGOS_COMMANDER_HAZARD_ZONES_H
#define MANGOS_COMMANDER_HAZARD_ZONES_H

#include "Common.h"
#include "ObjectGuid.h"
#include "Commander/Perception/WorldModel.h"
#include <vector>

namespace Commander
{
    enum class HazardKind : uint8
    {
        Burst,      // area around the caster when it casts the spell
        Pulse,      // area around the caster every few seconds (periodic trigger aura)
        Range,      // single target control: anyone within spell range may be picked
        Ground,     // a spell area on the ground (WorldModel::groundAreas), every tick until it ends
    };

    // Roles a zone is dangerous to (bit mask).
    enum HazardRoles : uint8
    {
        HAZARD_HEALER = 0x1,
        HAZARD_RANGED = 0x2,
        HAZARD_MELEE  = 0x4,
        // Areas that do not move with the enemy the tank holds: ability sheet areas everyone leaves
        // while being cast (BOSS_ABILITY_EVERYONE_LEAVES), areas on the ground and at a cast's
        // destination, and boss module zones that say so (ScriptedHazard::roles).
        HAZARD_TANK   = 0x8,
    };

    struct HazardZone
    {
        ObjectGuid enemy;
        uint32 spellId = 0;             // spell list / EventAI spell, or the aura for pulses
        uint32 hitSpellId = 0;          // spell whose effects land (the triggered spell for pulses)
        HazardKind kind = HazardKind::Burst;
        Position3 center;               // enemy position at the last refresh
        float radius = 0.0f;            // spell radius (Range: max range) + RADIUS_MARGIN
        uint8 roles = 0;                // HazardRoles
        bool control = false;
        bool breaksOnDamage = false;
        float controlSec = 0.0f;
        float damage = 0.0f;            // per hit, direct + all periodic ticks, before resistances
        bool periodic = false;          // most of the damage is a periodic aura (Melt Ore)
        float intervalSec = 0.0f;       // Pulse: aura period; Burst / Range: average repeat delay
        bool casting = false;           // the enemy is casting this spell now
        float castRemainingSec = 0.0f;
        float hitChance = 1.0f;         // Burst / Pulse 1; Range 1 / members in range
        // A cone (Burst only, boss doc G4): full width in radians, < 0 behind the enemy, 0 = a
        // circle. `facing`: EnemyFacts::facing; `aim`: the unit a front cone points at (its
        // victim or cast target), who cannot leave it: the cone turns with it.
        float arc = 0.0f;
        float facing = 0.0f;
        ObjectGuid aim;
        // The centre is a place, not the enemy (Ground zones, casts at a destination, boss module
        // zones at a fixed point): it does not follow anyone, stepping out of it holds.
        bool onGround = false;
        // Seconds until it is gone (Ground: the dynamic object's time left), 0 = not known.
        float endsInSec = 0.0f;

        bool DangerousTo(uint8 roleBit) const { return (roles & roleBit) != 0; }
        bool Cone() const { return arc != 0.0f; }
    };

    namespace HazardZones
    {
        static constexpr float RADIUS_MARGIN = 1.0f;
        // Range zones only for spell ranges in [MIN, MAX]: melee range control cannot be
        // avoided by a healer anyway, and 40+ yd covers every sensible healing spot.
        static constexpr float RANGE_ZONE_MIN = 8.0f;
        static constexpr float RANGE_ZONE_MAX = 36.0f;

        // HAZARD_x bit for a CombatBotRoles value, 0 for unknown roles.
        uint8 RoleBit(uint8 role);

        // All zones of all enemies in `world`. Pure, uncached.
        std::vector<HazardZone> Compute(WorldModel const& world);

        // Compute() cached per thread for one (world, version, refresh time), like the
        // CastValues planning cache: every bot asks the same question once per tick. The
        // reference is valid until this thread calls Get for another world model.
        std::vector<HazardZone> const& Get(WorldModel const& world);

        // Points lost by `member` per hit of `zone` (damage + control, no death penalty,
        // hitChance not applied).
        float HitPoints(WorldModel const& world, HazardZone const& zone, PartyMemberFacts const& member);

        // 3D distance from the zone centre (same metric as Spell.cpp PUSH_SRC_CENTER).
        float DistanceTo(HazardZone const& zone, Position3 const& pos);

        // `pos` is in the zone: within the radius less `marginYd`, and for a cone within its
        // arc less `marginYd` at that distance (SpellTags::InCone).
        bool Covers(HazardZone const& zone, Position3 const& pos, float marginYd = 0.0f);
        // Yards from `pos` to the zone's edge (0 outside): radially, or for a cone also sideways
        // out of its arc, whichever is shorter.
        float ExitYd(HazardZone const& zone, Position3 const& pos);

        // True if `pos` is inside a zone of `zones` that is dangerous to `roleBit`. `spare`: a
        // member whose own position is asked; the cones aimed at it do not count (they turn
        // with it). TeamBotAI asks this before following the leader in combat.
        bool IsInside(std::vector<HazardZone> const& zones, uint8 roleBit, Position3 const& pos,
                      ObjectGuid spare = ObjectGuid());

        char const* KindName(HazardKind kind);
    }
}

#endif
