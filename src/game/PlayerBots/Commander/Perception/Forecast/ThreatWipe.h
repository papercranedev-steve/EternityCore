/*
 * PartyBot Commander layer 1 (eyes): when an enemy next wipes its victim's threat (2026-11-26,
 * strat-20261126-224508: Patchwork Horror's Knock Away every 7-15 s took the druid tank's whole
 * threat, the next on its list, the healer, got it; Growl's 10 s cooldown missed every other one).
 *
 * The enemy's spells that wipe their target's threat: SpellTags::WipesTargetThreat on the spells
 * of its profile (Tags TAG_THREAT_WIPE). Only those cast on its current victim (creature_spells
 * castTarget TARGET_T_HOSTILE, or unknown for EventAI).
 * When, seconds from the world's refresh:
 *   creature_spells row with a live timer (EnemyFacts::spellList): its cooldown left, up to one
 *     list check (LIST_CHECK_SEC) later;
 *   else the profile's timers: seen cast (EnemyFacts::LastCastStartMs) -> start + repeat min/max,
 *     never seen -> the fight's start (firstSeenMs) + initial min/max.
 *   A chance below 100 or a missing timer: no forecast (`valid` false).
 * Pure: WorldModel facts and the static profile.
 */

#ifndef MANGOS_COMMANDER_THREAT_WIPE_H
#define MANGOS_COMMANDER_THREAT_WIPE_H

#include "Commander/Perception/WorldModel.h"

namespace Commander
{
namespace ThreatWipe
{
    static constexpr float LIST_CHECK_SEC = 1.2f;     // CreatureAI::UpdateSpellsList

    struct Next
    {
        bool valid = false;
        uint32 spellId = 0;
        float minSec = 0.0f;
        float maxSec = 0.0f;
        float MidSec() const { return 0.5f * (minSec + maxSec); }
    };

    // `e`'s next wipe of its victim's threat (the soonest of its wipe spells).
    Next Of(EnemyFacts const& e, uint32 nowMs);
}
}

#endif
