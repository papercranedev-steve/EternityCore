/*
 * PartyBot Commander layer 1: what a chained heal (Chain Heal) does on the members it jumps to
 * after its first target (user 2026-11-15: until then only the first target was counted, so
 * Chain Heal lost to Healing Wave even with the whole party hurt).
 *
 * The jumps as the core picks them (Spell.cpp, TARGET_UNIT_FRIEND_CHAIN_HEAL): from the
 * previous target, the first member within CHAIN_SPELL_JUMP_RADIUS that is not at full health,
 * party players by most health missing first, then the others (pets). Jump k heals the first
 * target's amount x multiplier^k (SpellEntry::DmgMultiplier with SPELLMOD_EFFECT_PAST_FIRST;
 * the +healing coefficient shrinks the same way, SpellEntry::CalculateCustomCoefficient), the
 * number of targets is EffectChainTarget with SPELLMOD_JUMP_TARGETS. Line of sight between the
 * targets is not checked (party members a few yards apart).
 *
 * Each jump is capped like the first target's heal: at most the health missing when it lands
 * (missing now + Value::TakenDps x cast time). Map thread (reads the healer's spell mods).
 */

#ifndef MANGOS_COMMANDER_CHAIN_HEAL_FORECAST_H
#define MANGOS_COMMANDER_CHAIN_HEAL_FORECAST_H

#include "Commander/Perception/WorldModel.h"

class TeamBotAI;
class SpellEntry;

namespace Commander
{
namespace ChainHealForecast
{
    struct Jumps
    {
        float heal = 0.0f;          // expected healing on the jump targets
        float effective = 0.0f;     // of it, capped at what they miss when it lands
        uint32 count = 0;           // jump targets found
    };

    // `spell` cast by `bot` on `first`, landing after `castSec`; nothing for a spell that does
    // not chain.
    Jumps Of(TeamBotAI const* bot, WorldModel const& world, PartyMemberFacts const& first,
             SpellEntry const* spell, float castSec);
}
}

#endif
