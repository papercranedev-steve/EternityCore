/*
 * PartyBot Commander layer 1: what a party heal around the caster (Prayer of Healing) does on
 * each member it reaches (party heal doc .kiro/docs/2026-11-28-party-heal-renew.md, user
 * 2026-11-28).
 *
 * The targets as the core picks them (Spell::FillRaidOrPartyTargets for
 * TARGET_ENUM_UNITS_PARTY_WITHIN_CASTER_RANGE): the caster, the players of its group's subgroup
 * (no group: itself) within the effect radius, and their pets; level + 10 >= the spell's
 * level. Members of the team in another subgroup, or out of the radius now, are not counted
 * (the radius is the spell's, without the core's leeway: a member at the edge may be missed).
 *
 * Per target: heal = HealSpells::Estimate (+healing and talents from the core), capped at the
 * health missing when it lands: missing now + Value::TakenDps x cast time, minus the direct
 * heals of the others landing before it (PartyMemberFacts::pendingHeal) and the HoT ticks
 * before it (PartyMemberFacts::hots). Map thread.
 */

#ifndef MANGOS_COMMANDER_PARTY_HEAL_FORECAST_H
#define MANGOS_COMMANDER_PARTY_HEAL_FORECAST_H

#include "Commander/Perception/WorldModel.h"

class TeamBotAI;
class SpellEntry;

namespace Commander
{
namespace PartyHealForecast
{
    struct Reach
    {
        ObjectGuid guid;
        float heal = 0.0f;          // expected healing on it
        float effective = 0.0f;     // of it, capped at what it misses when it lands
        float missing = 0.0f;       // health missing when it lands (0 = full)
    };

    struct Result
    {
        std::vector<Reach> targets;
        float heal = 0.0f;          // on every target
        float effective = 0.0f;
        float hurtHeal = 0.0f;      // on the targets that miss health when it lands
        uint32 hurt = 0;            // those targets
    };

    // The spell heals the caster's party around it (a heal effect on
    // TARGET_ENUM_UNITS_PARTY_WITHIN_CASTER_RANGE).
    bool IsPartyHeal(SpellEntry const* spell);

    // `spell` cast by `bot` now, landing after `castSec`; empty for a spell that is no party heal.
    Result Of(TeamBotAI const* bot, WorldModel const& world, SpellEntry const* spell, float castSec);
}
}

#endif
