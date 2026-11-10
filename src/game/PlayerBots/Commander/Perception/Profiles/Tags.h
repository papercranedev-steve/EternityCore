/*
 * PartyBot Commander layer 1 (eyes): tags, the plain kinds of enemies and of the party the
 * doctrine chooses by (doc .kiro/docs/2026-11-09-doctrine section 3). Facts only, no choice;
 * each reader looks at the one or two it needs.
 *
 * Engage (3.1), how an enemy fights:
 *   StandBack  stands back and casts its main ranged spell (CreatureProfile::mainRangedSpellId),
 *              or its melee is off (walking bombs, casters standing back). It walks in only when
 *              its cast fails (no sight, out of range, silenced).
 *   Melee      everything else: runs to whoever it hits.
 * Tier (3.1), how much it takes, relative to this party and this fight:
 *   Fodder     not elite and the party kills it within FODDER_TTK_SEC (max health / DealtDps):
 *              crate insects, summoned skeletons.
 *   Normal     not elite (an outdoor bear, a Blackrock orc in the open world).
 *   Elite      elite.
 *   Heavy      elite with at least HEAVY_HEALTH_MULT x the median max health of the elites in
 *              this fight (Patchwork Horror among ghouls). One elite alone is just Elite.
 *   Max health, not health: a tier does not change while the enemy is being hit.
 * TeamLedger (3.2), what the party can do now:
 *   areaDealers  damage dealer bots that can act, with area spells (ClassKit::AreaDamage) and
 *                the mana for the cheapest one, or channelling one now (a running Blizzard is
 *                not "ready" but still counts).
 *   mezTypes     creature types (1 << (type - 1)) the bots' controls that break on damage
 *                can take (ControlAbility::Kind Mez); ~0 = a control for any type.
 */

#ifndef MANGOS_COMMANDER_TAGS_H
#define MANGOS_COMMANDER_TAGS_H

#include "Commander/Perception/WorldModel.h"
#include <string>
#include <vector>

namespace Commander
{
    enum class Engage : uint8 { Melee, StandBack };
    enum class Tier : uint8 { Fodder, Normal, Elite, Heavy };
    // Where it came from (3.1 "source", todo 9): SpawnSources.h, else summoned, an ambusher
    // (stealth in its template), a patrol (waypoint default movement), or standing there.
    enum class Source : uint8 { Static, Summoned, Trap, DeathSpawn, Called, Patrol, Ambush };

    // Tactical roles (3.1, todo 9), bits of Tags::RolesOf. The recognitions the modules used to
    // do each for themselves, in one place (static data, the creature's profile and spells).
    enum : uint16
    {
        TAG_SELF_DESTRUCT = 1 << 0,     // a spell that kills its caster (Exploding Cadaver, Walking Bomb Effect)
        TAG_ALARM         = 1 << 1,     // brings more as it aggroes (CreatureProfile::aggroSpawns: Eye of Naxxramas)
        TAG_HEALER        = 1 << 2,     // a heal spell (SpellProfile CastCategory::Heal)
        TAG_SUMMONER      = 1 << 3,     // summons on a combat timer or with a summon spell
        TAG_MANA_DRAIN    = 1 << 4,     // mana burn / drain
        TAG_TOTEM         = 1 << 5,     // creature type totem (a ward, a totem)
        TAG_FLEES         = 1 << 6,     // runs for help at low health (CreatureProfile::fleeAtHealthPct)
        TAG_TAUNT_IMMUNE  = 1 << 7,     // CREATURE_IMMUNITY_TAUNT, or the override table's tauntMult 0
        TAG_CHARMER       = 1 << 8,     // mind control (charm / possess aura: Dominate Mind)
        TAG_RESURRECTS    = 1 << 9,     // brings the dead back (resurrect effect, Raise Dead)
    };

    // A self-destruct on the way: where, when (doctrine and the blast module).
    struct BlastFacts
    {
        bool valid = false;
        float radius = 0.0f;
        float damage = 0.0f;            // average, before armor
        bool physical = false;
        uint32 earliestMs = 0;          // WorldTimer ms (0 = unknown: no timer)
        uint32 latestMs = 0;
    };

    struct TeamLedger
    {
        uint32 areaDealers = 0;
        uint32 mezTypes = 0;
    };

    namespace Tags
    {
        static constexpr float FODDER_TTK_SEC = 4.0f;
        static constexpr float HEAVY_HEALTH_MULT = 2.0f;

        Engage EngageOf(EnemyFacts const& e);
        // Before a fight (no live melee flag): the profile only.
        Engage EngageOfEntry(uint32 entry);

        // The elites' median max health of this fight, for TierOf (0 = no elite).
        float EliteMedianHealth(WorldModel const& world);
        float MedianHealth(std::vector<uint32> eliteMaxHealth);
        Tier TierOf(WorldModel const& world, EnemyFacts const& e, float eliteMedianHealth);
        Tier TierOf(WorldModel const& world, bool elite, uint32 maxHealth, float eliteMedianHealth);

        TeamLedger LedgerOf(WorldModel const& world);

        Source SourceOf(EnemyFacts const& e);
        uint16 RolesOf(uint32 entry);
        // "healer, summoner" (empty = none).
        std::string RoleNames(uint16 roles);
        // Its self-destruct, timed from when it joined the fight (EnemyFacts::firstSeenMs: the
        // EventAI combat timer starts about then).
        BlastFacts BlastOf(EnemyFacts const& e);
        // A neutral (yellow) creature within `radius` of `at` (its centre): never to be touched
        // (user 2026-11-10). From WorldModel::packs (PackCreature::aggressive false).
        PackCreature const* NeutralNear(WorldModel const& world, Position3 const& at, float radius);

        char const* Name(Engage engage);
        char const* Name(Tier tier);
        char const* Name(Source source);
    }
}

#endif
