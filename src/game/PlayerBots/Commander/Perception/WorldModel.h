/*
 * PartyBot Commander layer 1 (perception): facts about one party on one map.
 * Filled by Perception/WorldModelBuilder.cpp, read-only for every other layer.
 * Plain data only (GUIDs, numbers), so the query functions in ThreatView /
 * PackResolver can later be tested on recorded snapshots without a server.
 */

#ifndef MANGOS_COMMANDER_WORLD_MODEL_H
#define MANGOS_COMMANDER_WORLD_MODEL_H

#include "Common.h"
#include "ObjectGuid.h"
#include "RoleCapability.h"
#include <algorithm>
#include <vector>

namespace Commander
{
    struct Position3
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    // A bot spell that can stop an enemy cast (interrupt, stun, silence).
    struct InterruptAbility
    {
        uint32 spellId = 0;
        float maxRange = 0.0f;       // spell range; melee abilities are ~5
        uint32 cooldownMs = 0;       // full cooldown, cost of using it
        uint32 lockoutMs = 0;        // school lockout of an interrupt effect (0 for stuns and silences)
        // Mechanics that stop the cast (1 << MECHANIC_x): spell mechanic and effect mechanics.
        // A target immune to all of them is not stopped (stun on a stun-immune boss).
        uint32 mechanicMask = 0;
        bool ready = false;          // off cooldown and enough power (target checks happen at cast time)
        // The target must face the caster (Gouge: Spell::CheckCast, attributes 0x150010). An
        // enemy faces its victim, so only its victim can count on it.
        bool needsFront = false;
    };

    // A friendly dispel a bot has learned (ClassKit::Dispels).
    struct DispelAbility
    {
        uint32 spellId = 0;
        uint32 dispelMask = 0;       // 1 << DispelType of every SPELL_EFFECT_DISPEL
        float maxRange = 0.0f;
        bool ready = false;          // off cooldown, enough mana, can act and cast
        // Can also target a member mind controlled by a creature (Dispel Magic; the friendly-only
        // ones fail on such a target: PartyBotAI::SelectDispelTarget).
        bool hostileOk = false;
    };

    // A crowd control spell a bot has learned (ClassKit::Controls): what it can take out of the
    // fight and for how long. Read from the spell data (Perception/ControlValue.h values it).
    struct ControlAbility
    {
        uint32 spellId = 0;
        uint32 creatureTypeMask = 0;    // SpellEntry::TargetCreatureType (1 << (type - 1)), 0 = any type
        uint32 mechanicMask = 0;        // 1 << (mechanic - 1): what an immune creature resists
        uint8 school = 0;
        float maxRange = 0.0f;          // single target: spell range
        float radius = 0.0f;            // > 0: every enemy within this of the caster (Psychic Scream)
        uint32 castMs = 0;
        float durationSec = 0.0f;       // full effect duration
        uint32 cooldownMs = 0;
        uint32 manaCost = 0;            // 0 for rage / energy abilities
        bool breaksOnDamage = false;    // AURA_INTERRUPT_DAMAGE_CANCELS: polymorph, sap, sleep
        bool fear = false;              // the target runs around (SPELL_AURA_MOD_FEAR)
        bool needsStealth = false;      // Sap
        bool peacefulOnly = false;      // target must not be in combat (Sap)
        // Off cooldown, enough power, reagents, right form, can act and cast. Target checks
        // (creature type, immunity, range, line of sight) are the user's.
        bool ready = false;
    };

    // The control effect an enemy is under now (the one lasting longest).
    struct ActiveControl
    {
        uint32 spellId = 0;             // 0 = none
        ObjectGuid caster;
        uint32 remainingMs = 0;
        bool breaksOnDamage = false;
        bool fear = false;
    };

    // A harmful, dispellable aura a creature put on a party member (players only).
    struct DebuffFacts
    {
        uint32 spellId = 0;
        uint8 dispelType = 0;        // SpellEntry::Dispel
        ObjectGuid caster;
        uint32 casterLevel = 0;
        uint32 remainingMs = 0;
        uint32 stacks = 1;
        // The caster is alive, hitting the member and its profile puts the spell back: seconds
        // until it does (on-hit proc: attack time / chance; spell list: repeat delay). 0 = not.
        float reapplySec = 0.0f;
    };

    // One direct heal of a healer bot, chain cast (Perception/HealCapacity.h).
    struct HealRate
    {
        float hps = 0.0f;           // healing per second: heal (crits included) / max(cast, GCD)
        float perMana = 0.0f;       // healing per mana
    };

    // A party member or a party member's pet on this map.
    struct PartyMemberFacts
    {
        ObjectGuid guid;
        ObjectGuid ownerGuid;       // pets: the owning member
        bool isBot = false;         // controlled by PartyBotAI
        bool isPet = false;
        bool alive = false;
        // CombatBotRoles. Bots: PartyBotAI::m_role. Players: the role they chose, else from
        // RoleCapability (tank only while no bot tanks), see Perception/Tanks.h. Pets: 0.
        uint8 role = 0;
        bool roleLocked = false;    // chosen by the user (bot or player); the "roles" assigner leaves it
        RoleCapability cap;         // what this member can do (players and bots, not pets)
        uint8 classId = 0;
        float healthPct = 0.0f;
        uint32 health = 0;
        uint32 maxHealth = 0;
        uint32 level = 0;
        uint32 armor = 0;
        float manaPct = -1.0f;      // -1 = no mana, or mana is not the active power (druid in bear / cat form)
        // Mana pool, also while shapeshifted into a form without mana. maxMana 0 = no mana pool.
        uint32 mana = 0;
        uint32 maxMana = 0;
        Position3 pos;
        ObjectGuid victim;          // who this member is attacking
        // Bots: the first live enemy carrying one of the raid marks the player told this bot to
        // focus (`.partybot focusmark`, PartyBotAI::m_marksToFocus). Empty = none. A player
        // order for TargetAssigner.
        ObjectGuid focusMarkEnemy;
        bool casting = false;       // busy with a cast or channel (cannot react this tick)
        uint32 castRemainingMs = 0; // of that cast or channel
        // Combo points (rogue, cat druid) and the enemy they are on: lost when it switches target.
        uint8 comboPoints = 0;
        ObjectGuid comboTarget;
        bool resting = false;       // eating, drinking or sitting
        bool inCombat = false;
        // Cannot act: stunned, asleep, feared, confused, polymorphed, or charmed by an enemy.
        // Assigners skip such members; rates do not count them as healers or damage dealers.
        bool controlled = false;
        bool charmed = false;       // mind controlled by an enemy: hostile to us, do not attack, dispel
        bool silenced = false;      // can still melee, cannot cast
        // Set up to tank right now: warrior in Defensive Stance, druid in (Dire) Bear Form,
        // paladin with Righteous Fury. A state, not who the tanks are (that is `role`, Tanks.h).
        bool tankStance = false;
        std::vector<InterruptAbility> interrupts;   // bots only
        std::vector<DispelAbility> dispels;         // bots only
        std::vector<ControlAbility> controls;       // bots only
        // Damage dealer bots with ClassKit::AreaDamage spells, on their victim (out of combat:
        // on itself): the best area
        // spell's damage per second on each target it hits, its radius, and the filler's
        // damage per second (what the area spell has to beat). 0 = none. For the area damage
        // a crowd control gives up (ControlValue::AreaLoss).
        float areaDpsPerTarget = 0.0f;
        float areaRadius = 0.0f;
        float fillerDps = 0.0f;
        // Bots whose filler (ClassKit::Filler) costs mana: mana per second while casting it
        // (cost / max(cast time, GCD)). What spent mana is worth to a damage dealer (ManaWorth.h).
        float fillerManaPerSec = 0.0f;
        std::vector<DebuffFacts> debuffs;           // players only
        // Damage actually taken (Perception/DamageIntake.h): per second over the last few
        // seconds, and the biggest spike within a short window. Value::TakenDps combines the
        // rate with the prediction from enemy melee.
        float takenDps = 0.0f;
        float takenBurst = 0.0f;
        // Players: mana regenerated per second outside / inside the five second rule (world
        // rate included), and ms left in the rule (0 = outside, spirit regeneration runs).
        float manaRegen = 0.0f;
        float manaRegenCasting = 0.0f;
        uint32 fiveSecondRuleMs = 0;
        // Healer-role bots: their direct heals with their real numbers (+healing gear,
        // talents, crit), valued on themselves (Perception/HealCapacity.h). Empty otherwise.
        std::vector<HealRate> healRates;

        bool CanAct() const { return alive && !controlled; }
        bool UsesMana() const { return maxMana > 0; }
    };

    // One row of an enemy's threat list, plus party members not on it yet (onList = false).
    struct ThreatEntry
    {
        ObjectGuid guid;
        float threat = 0.0f;
        bool onList = false;
        bool partySide = false;     // party member or pet of this party
        bool inMeleeReach = false;  // enemy can hit this unit in melee -> 110% rule instead of 130%
        float threatPerSec = 0.0f;  // over the last few seconds (ThreatHistory); includes taunt jumps
    };

    // When an enemy last started casting a spell (EnemyFacts::castStarts).
    struct CastStart
    {
        uint32 spellId = 0;
        uint32 lastStartMs = 0;         // WorldTimer ms the cast began (seen elapsed time subtracted)
    };

    struct EnemyFacts
    {
        ObjectGuid guid;
        uint32 entry = 0;
        uint32 level = 0;
        bool elite = false;
        float healthPct = 0.0f;
        uint32 health = 0;
        uint32 maxHealth = 0;
        float meleeHit = 0.0f;          // average main hand hit now (live unit fields, enrage included)
        float meleeDps = 0.0f;          // meleeHit / attack time, before the victim's armor
        Position3 pos;
        // UNIT_FIELD_BOUNDINGRADIUS: area spells and melee reach start at its edge, not its centre.
        float boundingRadius = 0.0f;

        ObjectGuid victim;              // threat manager's current victim (who it is hitting)
        float victimThreat = 0.0f;
        uint32 victimSinceMs = 0;       // WorldTimer ms when it started hitting `victim`
        uint32 castingSpellId = 0;
        ObjectGuid castTarget;          // unit target of the current cast, empty = none / area
        bool castChanneled = false;
        bool castInterruptible = false;
        uint8 castCategory = 0;         // CastCategory (SpellTags.h)
        uint32 castRemainingMs = 0;
        uint32 castTotalMs = 0;
        uint32 lastCastSpellId = 0;     // most recent interruptible cast seen this fight (kept after it ends)
        bool crowdControlled = false;   // cannot act (stun, fear, polymorph, ...)
        ActiveControl control;          // the control effect on it, if any (also from players)
        uint32 creatureTypeMask = 0;    // Unit::GetCreatureTypeMask: 1 << (CreatureType - 1)
        // Longest periodic damage a party member put on it, ms left (0 = none): it wakes a
        // control that breaks on damage at the next tick.
        uint32 partyDotMs = 0;
        bool ignoresAssistThreat = false; // heal/buff threat is zeroed (fear, confuse, breakable stun)
        // False while it cannot be attacked and does nothing: feign death phases (not selectable),
        // total immunity (divine shield), evading. Such enemies are left out of damage rates,
        // time-to-die shares, taunts and heal-threat losses.
        bool acting = true;
        bool meleeEnabled = true;       // AI melee on (walking bombs, casters standing back turn it off)
        bool meleePhysical = true;      // false: melee is fire/frost/... (armor does not reduce it)
        // Main ranged spell damage per second (CreatureProfile::mainRangedSpellId), counted
        // while it stands back casting (melee off). Before resistances.
        float rangedDps = 0.0f;
        bool silenced = false;          // UNIT_FLAG_SILENCED: no casts, no interrupter needed
        // Mana now and the creature's base mana (GetCreateMana, for ManaCostPercentage).
        // maxMana 0 = no mana pool. A caster out of mana casts nothing that costs mana
        // (strat-20261101-224637: a Skeletal Guardian at 23 mana kept "casting" Arcane Explosion).
        uint32 mana = 0;
        uint32 maxMana = 0;
        uint32 createMana = 0;
        bool summoned = false;          // temporary summon or guardian (adds)
        ObjectGuid summoner;            // who summoned it, if known
        uint32 firstSeenMs = 0;         // WorldTimer ms of the first snapshot it was in (ThreatHistory)
        uint32 rank = 0;                // creature_template.rank (also correct for guardians)
        // No party member or pet is on its threat list: it is here because it fights one of
        // world.allies (combat scenario 9: adds hitting the Zul'Farrak prisoners). Its threat
        // list then only has ally rows (partySide = false) plus our not-listed rows.
        bool onAllyOnly = false;

        // Every spell (interruptible or not) seen cast by it since it joined our lists, with the
        // start of its latest cast. Creature repeat delays run from the cast start, so
        // refreshedAtMs - lastStartMs vs the repeat delay tells how soon it can come again.
        std::vector<CastStart> castStarts;
        // Bots with a ranged interrupt that have no line of sight to it. Filled only while it
        // casts an interruptible spell (empty otherwise: line of sight not checked).
        std::vector<ObjectGuid> outOfSight;

        // 0 = not seen casting `spellId`.
        uint32 LastCastStartMs(uint32 spellId) const
        {
            for (CastStart const& c : castStarts)
                if (c.spellId == spellId)
                    return c.lastStartMs;
            return 0;
        }
        bool InSightOf(ObjectGuid member) const
        {
            return std::find(outOfSight.begin(), outOfSight.end(), member) == outOfSight.end();
        }

        // Sorted by threat, highest first. Every row of the live threat list (party members,
        // pets, allies and anyone else on it, partySide tells them apart), plus every party
        // member and pet on this map that is not on it yet (onList = false).
        std::vector<ThreatEntry> threat;
    };

    // A friendly creature near the party that is not in the party and is fighting: escorted
    // NPCs, freed prisoners (Zul'Farrak pyramid), guards. Filled by WorldModelBuilder.
    struct AllyFacts
    {
        ObjectGuid guid;
        uint32 entry = 0;
        uint32 level = 0;
        uint32 rank = 0;                // creature_template.rank
        bool elite = false;
        float healthPct = 0.0f;
        uint32 health = 0;
        uint32 maxHealth = 0;
        uint32 armor = 0;
        Position3 pos;
        ObjectGuid victim;              // who it is attacking
        // Holds enemies like a tank: damage dealers leave room under its threat and the tank does
        // not take its enemies (ThreatBehavior). Builder rule: elite and max health at least
        // ALLY_TANK_HEALTH_MULT x the party tank's (Sergeant Bly 18500 vs a level 45 tank ~4000).
        // Boss modules may overwrite it in AddFacts (the Zul'Farrak module keeps it on Bly only).
        bool tankLike = false;
    };

    enum class PackSource : uint8
    {
        Database,   // creature_groups
        Distance,   // no group record, linked by distance
        Mixed,      // a creature_groups pack plus ungrouped creatures next to it
    };

    // Creatures near the party that are not in combat.
    struct PackCreature
    {
        ObjectGuid guid;
        ObjectGuid dbGroupLeader;   // creature_groups leader, empty if none
        uint32 entry = 0;
        uint32 level = 0;
        bool elite = false;
        bool aggressive = false;    // hostile (red); false = neutral (yellow)
        bool stealthed = false;     // the tank cannot target it now (still joins the fight)
        bool patrol = false;        // walks a waypoint path; its pack membership changes as it moves
        bool tappedByOther = false; // somebody outside the party has loot rights
        bool boss = false;          // CreatureProfile::boss: pull and clear leave it to the player
        bool noAggro = false;       // CREATURE_FLAG_EXTRA_NO_AGGRO: never starts a fight
        uint32 creatureTypeMask = 0;    // 1 << (CreatureType - 1)
        Position3 pos;
        float reach = 0.0f;             // aggro range against the main tank while fighting, else this bot
        uint32 health = 0;
        uint32 maxHealth = 0;
        float meleeDps = 0.0f;          // estimate before armor (CreatureProfiles::EstimateMeleeDps)
    };

    // A creature walking a waypoint path, not in combat (WorldModel::patrols).
    struct PatrolFacts : PackCreature
    {
        // Filled only while the party fights (HasPartyEnemy), for the fight-time patrol check
        // (Coordination/PatrolAssigner.h): where it will be every trackStepSec from the refresh
        // (PatrolTracker::Predict, PATROL_FIGHT_HORIZON_SEC ahead; track[0] = now).
        float trackStepSec = 0.0f;
        std::vector<Position3> track;
    };

    struct PackFacts
    {
        PackSource source = PackSource::Distance;
        ObjectGuid dbGroupLeader;
        std::vector<PackCreature> members;
        Position3 center;
        float distanceToLeader = 0.0f;  // party leader to pack center
        uint32 eliteCount = 0;
        uint32 maxLevel = 0;
    };

    // Fight-wide rates for the common value unit (value-scoring doc section 3).
    // Filled by CombatRates (Perception/CombatRates.h) on every refresh.
    struct RateFacts
    {
        float partyMaxHealth = 0.0f;    // sum over alive and dead members, pets excluded; 1 point = 1% of it
        // Melee of enemies hitting us after armor, plus the main ranged spell of casters standing
        // back (EnemyFacts::rangedDps). Other spells are not included yet.
        float incomingDps = 0.0f;
        float dealtDps = 0.0f;          // measured drop of enemy health per second
        bool dealtMeasured = false;     // false = not enough samples yet, dealtDps is an estimate
        // The last fight's damage per second over its whole length (CombatRates), 0 = none
        // yet, and how many damage dealers the party had then. Value::DealtDps uses it
        // before the next fight is measured.
        float lastFightDps = 0.0f;
        uint32 lastFightDealers = 0;
        float enemyHealth = 0.0f;       // current health of all enemies
        uint32 healers = 0;             // healer-role bots that can act (alive, not controlled)
        uint32 damageDealers = 0;       // members that can act and are not healers (tanks included)
    };

    struct WorldModel
    {
        uint32 version = 0;          // +1 on every refresh
        uint32 refreshedAtMs = 0;
        uint32 packsRefreshedAtMs = 0;

        ObjectGuid leader;           // party leader (the player)
        std::vector<PartyMemberFacts> party;
        // Creatures with a party member or pet on their threat list, then (onAllyOnly = true)
        // creatures fighting one of `allies` only.
        std::vector<EnemyFacts> enemies;
        // Creatures a member (the player, a bot, a pet) is attacking that have none of us on
        // their threat list yet and are not in `enemies`: the player just pressed attack and
        // has not hit yet, a bot was sent at one. Not a fight yet (HasPartyEnemy ignores them);
        // TargetAssigner picks the first target of a fight from them.
        std::vector<EnemyFacts> targeted;
        // Friendly non-party creatures in combat near the leader (at most MAX_ALLIES, nearest first).
        std::vector<AllyFacts> allies;
        // Builder bookkeeping: friendly creatures in combat found by the last grid scan
        // (RefreshPacks, once a second), re-read by every RefreshCombat. Not a fact to act on.
        std::vector<ObjectGuid> allyCandidates;
        // Builder bookkeeping: cast starts per (enemy, spell), kept across refreshes (enemies
        // are rebuilt each time) and copied into EnemyFacts::castStarts. Entries of enemies
        // that left the lists are dropped. Read EnemyFacts::castStarts instead.
        struct CastStartRecord
        {
            ObjectGuid enemy;
            CastStart cast;
        };
        std::vector<CastStartRecord> castStartLog;
        std::vector<PackFacts> packs;       // nearby creatures not in combat
        // Out-of-combat creatures walking a waypoint path within PATROL_SCAN_RADIUS of the
        // leader (WorldModelBuilder.h), also those already in `packs`. Long loops reach the
        // party from beyond the pack scan (Patchwork Horror, strat-20261101-225111).
        std::vector<PatrolFacts> patrols;
        RateFacts rates;

        PartyMemberFacts const* FindMember(ObjectGuid guid) const;
        EnemyFacts const* FindEnemy(ObjectGuid guid) const;
        EnemyFacts const* FindTargeted(ObjectGuid guid) const
        {
            for (EnemyFacts const& e : targeted)
                if (e.guid == guid)
                    return &e;
            return nullptr;
        }
        AllyFacts const* FindAlly(ObjectGuid guid) const
        {
            for (AllyFacts const& a : allies)
                if (a.guid == guid)
                    return &a;
            return nullptr;
        }
        // At least one enemy has a party member or pet on its threat list (a fight of ours,
        // not only an ally's).
        bool HasPartyEnemy() const
        {
            for (EnemyFacts const& e : enemies)
                if (!e.onAllyOnly)
                    return true;
            return false;
        }
    };
}

#endif
