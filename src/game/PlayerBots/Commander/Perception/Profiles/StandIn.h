/*
 * PartyBot Commander layer 1 (eyes): who can hold the enemies when the tank is gone, and how
 * (user 2026-11-15, contingencies: "a hunter's pet, a rogue, a warrior hold on"; a warrior
 * holds without a shield for now, a weapon swap comes later).
 *
 * Bots and bots' pets only (a player decides for himself), that can act, not healer bots
 * (nobody would heal then; the brain weighs the healer side apart):
 *   Pet     a pet with a threat ability (PartyMemberFacts::canOffTank): Growl / Torment on
 *           autocast already holds everything it is hit by without a main tank (ThreatBehavior).
 *   Stance  a warrior: Defensive Stance, Taunt, its own armor.
 *   Bear    a druid that knows Bear Form (RoleCapability::tank > 0): its armor in the form.
 *   Fury    a paladin: Righteous Fury, no taunt (RoleCapability::tank > 0).
 * Rogues (Evasion) are not tanks: they keep one enemy for a while (survival, the rescue).
 *
 * armorMult: the stand-in's armor once set up, over its armor now (FightLoad::Lineup). Bear
 * Form multiplies the armor from items (+180%, Dire Bear Form +360%); the facts have only the
 * total (agility too), so these are rough, untested guesses. 1 once in the form.
 * Pure.
 */

#ifndef MANGOS_COMMANDER_STAND_IN_H
#define MANGOS_COMMANDER_STAND_IN_H

#include "Commander/Perception/WorldModel.h"

namespace Commander
{
    enum class StandInWay : uint8
    {
        None,
        Pet,
        Stance,
        Bear,
        Fury,
    };

    struct StandInFacts
    {
        StandInWay way = StandInWay::None;
        float armorMult = 1.0f;
    };

    namespace StandIn
    {
        float const BEAR_ARMOR_MULT = 2.2f;
        float const DIRE_BEAR_ARMOR_MULT = 3.5f;
        uint32 const DIRE_BEAR_LEVEL = 40;

        StandInFacts Of(PartyMemberFacts const& m);
        // "the pet", "the warrior (Defensive Stance)", "the druid (bear form)"
        char const* Describe(StandInWay way);
    }
}

#endif
