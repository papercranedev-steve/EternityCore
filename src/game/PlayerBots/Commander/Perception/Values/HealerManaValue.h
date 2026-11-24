/*
 * PartyBot Commander layer 1 (eyes): what more mana for a healer saves beyond the healing it
 * buys: the death of the member the enemies hit when the healing runs dry before the fight
 * ends (user 2026-11-25: a healer out of mana in a long fight loses the tank, and with nobody
 * to take over the team; a damage dealer lost to a short off-tank is survivable).
 *
 * The fight forecast (Perception/Forecast/FightLoad.h, FromWorld: every enemy on the tank)
 * run twice, the same fight with and without `regenPerSec` more regeneration on `healer` for
 * `sec` seconds (an Innervate being valued):
 *   healers   the healer bots, plus players in the healer role (FightLoad::Lineup::spareHealers,
 *             their heals at PLAYER_HEAL_SHARE); every healer bot's ready mana consumables
 *             (PartyMemberFacts::manaItems) drunk once they fit (FightLoad::Healer::itemMana):
 *             a healer that still has a potion is not dry yet, so a costly boost (a druid tank
 *             leaving bear form) is the last resort by its value, not by a rule.
 *   points    Value::HolderDeathPenalty(hit taker) x (FightLoad::Risk(without) - Risk(with)),
 *             0 when the boost changes nothing.
 * Only the death: the healing the mana buys is ManaWorth's (BoostValue adds both).
 * 0 before the party's damage is measured (the fight length is a guess then, as ManaBudget).
 * Pure: WorldModel facts.
 */

#ifndef MANGOS_COMMANDER_HEALER_MANA_VALUE_H
#define MANGOS_COMMANDER_HEALER_MANA_VALUE_H

#include "Commander/Perception/WorldModel.h"

namespace Commander
{
    namespace HealerManaValue
    {
        // Its heals are in the fight forecast: a healer bot, or a player in the healer role
        // whose heals are known (spareHealRates).
        bool Counts(PartyMemberFacts const& m);

        float DeathAvoided(WorldModel const& world, PartyMemberFacts const& healer, float regenPerSec, float sec);
    }
}

#endif
