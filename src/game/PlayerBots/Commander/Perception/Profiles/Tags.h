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

namespace Commander
{
    enum class Engage : uint8 { Melee, StandBack };
    enum class Tier : uint8 { Fodder, Normal, Elite, Heavy };

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

        // The elites' median max health of this fight, for TierOf (0 = no elite).
        float EliteMedianHealth(WorldModel const& world);
        Tier TierOf(WorldModel const& world, EnemyFacts const& e, float eliteMedianHealth);

        TeamLedger LedgerOf(WorldModel const& world);

        char const* Name(Engage engage);
        char const* Name(Tier tier);
    }
}

#endif
