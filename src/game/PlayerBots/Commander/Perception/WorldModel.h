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
        float manaPct = -1.0f;      // -1 = no mana
        Position3 pos;
        ObjectGuid victim;          // who this member is attacking
        bool casting = false;       // busy with a cast or channel (cannot react this tick)
        bool resting = false;       // eating, drinking or sitting
        bool inCombat = false;
        std::vector<InterruptAbility> interrupts;   // bots only
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
        Position3 pos;

        ObjectGuid victim;              // threat manager's current victim (who it is hitting)
        float victimThreat = 0.0f;
        uint32 victimSinceMs = 0;       // WorldTimer ms when it started hitting `victim`
        uint32 castingSpellId = 0;
        bool castChanneled = false;
        bool castInterruptible = false;
        uint8 castCategory = 0;         // CastCategory (SpellTags.h)
        uint32 castRemainingMs = 0;
        uint32 castTotalMs = 0;
        uint32 lastCastSpellId = 0;     // most recent cast seen this fight (kept after it ends)
        bool crowdControlled = false;   // cannot act (stun, fear, polymorph, ...)
        bool ignoresAssistThreat = false; // heal/buff threat is zeroed (fear, confuse, breakable stun)

        // Sorted by threat, highest first. Contains every party member and pet on
        // this map, also those with no threat on this enemy.
        std::vector<ThreatEntry> threat;
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

    struct WorldModel
    {
        uint32 version = 0;          // +1 on every refresh
        uint32 refreshedAtMs = 0;
        uint32 packsRefreshedAtMs = 0;

        ObjectGuid leader;           // party leader (the player)
        std::vector<PartyMemberFacts> party;
        std::vector<EnemyFacts> enemies;    // creatures with a party member or pet on their threat list
        std::vector<PackFacts> packs;       // nearby creatures not in combat

        PartyMemberFacts const* FindMember(ObjectGuid guid) const;
        EnemyFacts const* FindEnemy(ObjectGuid guid) const;
    };
}

#endif
