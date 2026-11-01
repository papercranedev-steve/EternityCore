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
    };

    // A party member or a party member's pet on this map.
    struct PartyMemberFacts
    {
        ObjectGuid guid;
        ObjectGuid ownerGuid;       // pets: the owning member
        bool isBot = false;         // controlled by PartyBotAI
        bool isPet = false;
        bool alive = false;
        uint8 role = 0;             // CombatBotRoles for bots, ROLE_INVALID (0) for players and pets
        uint8 classId = 0;
        float healthPct = 0.0f;
        uint32 health = 0;
        uint32 maxHealth = 0;
        uint32 level = 0;
        uint32 armor = 0;
        float manaPct = -1.0f;      // -1 = no mana
        Position3 pos;
        ObjectGuid victim;          // who this member is attacking
        bool casting = false;       // busy with a cast or channel (cannot react this tick)
        bool resting = false;       // eating, drinking or sitting
        bool inCombat = false;
        // Cannot act: stunned, asleep, feared, confused, polymorphed, or charmed by an enemy.
        // Assigners skip such members; rates do not count them as healers or damage dealers.
        bool controlled = false;
        bool charmed = false;       // mind controlled by an enemy: hostile to us, do not attack, dispel
        bool silenced = false;      // can still melee, cannot cast
        std::vector<InterruptAbility> interrupts;   // bots only

        bool CanAct() const { return alive && !controlled; }
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
        bool summoned = false;          // temporary summon or guardian (adds)
        ObjectGuid summoner;            // who summoned it, if known
        uint32 firstSeenMs = 0;         // WorldTimer ms of the first snapshot it was in (ThreatHistory)
        uint32 rank = 0;                // creature_template.rank (also correct for guardians)
        // No party member or pet is on its threat list: it is here because it fights one of
        // world.allies (combat scenario 9: adds hitting the Zul'Farrak prisoners). Its threat
        // list then only has ally rows (partySide = false) plus our not-listed rows.
        bool onAllyOnly = false;

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
        Position3 pos;
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
        // Friendly non-party creatures in combat near the leader (at most MAX_ALLIES, nearest first).
        std::vector<AllyFacts> allies;
        // Builder bookkeeping: friendly creatures in combat found by the last grid scan
        // (RefreshPacks, once a second), re-read by every RefreshCombat. Not a fact to act on.
        std::vector<ObjectGuid> allyCandidates;
        std::vector<PackFacts> packs;       // nearby creatures not in combat
        RateFacts rates;

        PartyMemberFacts const* FindMember(ObjectGuid guid) const;
        EnemyFacts const* FindEnemy(ObjectGuid guid) const;
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
