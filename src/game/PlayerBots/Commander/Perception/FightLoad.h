/*
 * PartyBot Commander layer 1: can the party carry a fight against these creatures?
 * (user, 2026-11-03: a patrol like Patchwork Horror is worth waiting for, a bat is not; score
 * the fight, pull at once when the party can take it.)
 *
 * A rough fight run in STEP_SEC steps, every creature on the tank:
 *   damage   each present creature's melee per second on the tank (live damage fields, after
 *            the tank's armor), or its main ranged spell (WorldModelBuilder::RangedSpellDps)
 *            when that is more, x DAMAGE_MARGIN (other spells, crushing blows, a slow kill).
 *            A creature with arriveSec > 0 (a patrol coming later) hits from then on.
 *   kills    the party's damage per second (Value::DealtDps: this fight, else the last one)
 *            goes to the present creature with the most damage per remaining health first.
 *   healing  healer bots (PartyMemberFacts::healRates, Perception/HealCapacity.h: real spells,
 *            +healing gear) cover the damage and refill the tank's deficit over TOP_UP_SEC,
 *            each with the cheapest heal that keeps up (else its fastest), paying mana for the
 *            heal plus ManaBudget::OVERHEAL waste, regenerating at its in-combat rate.
 *            Player healers are not counted (their spells are not known).
 * The fight ends when no present creature is left; one that arrives after that is another
 * fight. Bearable: the tank survives and never drops below TANK_FLOOR_PCT.
 *
 * Simulate / Bearable / Score / Describe are pure. Build and FoeOf read live objects (map
 * thread, or the world thread while maps are idle).
 */

#ifndef MANGOS_COMMANDER_FIGHT_LOAD_H
#define MANGOS_COMMANDER_FIGHT_LOAD_H

#include "Commander/Perception/WorldModel.h"
#include <string>
#include <vector>

class Creature;
class Map;

namespace Commander
{
namespace FightLoad
{
    float const STEP_SEC = 0.5f;
    float const MAX_SEC = 300.0f;
    float const DAMAGE_MARGIN = 1.2f;
    float const TOP_UP_SEC = 5.0f;
    float const TANK_FLOOR_PCT = 30.0f;

    struct Foe
    {
        ObjectGuid guid;
        float health = 0.0f;
        float dps = 0.0f;           // on the tank, after armor, before DAMAGE_MARGIN
        float arriveSec = 0.0f;     // joins the fight this many seconds after the start
    };

    struct Healer
    {
        std::vector<HealRate> rates;
        float mana = 0.0f;
        float maxMana = 0.0f;
        float regenPerSec = 0.0f;   // inside the five second rule
    };

    struct Input
    {
        std::vector<Foe> foes;
        float partyDps = 1.0f;
        float tankHealth = 0.0f;
        float tankMaxHealth = 1.0f;
        std::vector<Healer> healers;
    };

    struct Result
    {
        float fightSec = 0.0f;
        float damage = 0.0f;        // on the tank, margin included
        float healed = 0.0f;
        float peakDps = 0.0f;       // highest damage per second on the tank, margin included
        float lowestTankPct = 100.0f;
        float manaStart = 0.0f;     // all healer bots together
        float manaLeft = 0.0f;
        bool tankDies = false;
        bool unfinished = false;    // still fighting after MAX_SEC
        float healthLeftPct = 0.0f; // share of the creatures' health left when it ended
    };

    Result Simulate(Input const& in);
    bool Bearable(Result const& r);
    // Higher is better: the tank's lowest health percent; a death or an unfinished fight:
    // minus the share of creature health left.
    float Score(Result const& r);
    // "fight ~62 s, tank down to 41%, incoming up to 520/s, healer mana 5400 -> 1200"
    std::string Describe(Result const& r);

    // The fight against `creatures` (live, alive ones) with the party's main tank and healer
    // bots as they are now. False when the party has no alive tank.
    bool Build(WorldModel const& world, Map* map, std::vector<ObjectGuid> const& creatures, Input& out);
    Foe FoeOf(Creature const* creature, PartyMemberFacts const& tank);
}
}

#endif
