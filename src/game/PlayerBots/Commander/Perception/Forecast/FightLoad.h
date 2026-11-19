/*
 * PartyBot Commander layer 1: can the party carry a fight against these creatures?
 * (user, 2026-11-03: a patrol like Patchwork Horror is worth waiting for, a bat is not; score
 * the fight, pull at once when the party can take it.)
 *
 * A rough fight run in STEP_SEC steps, every creature on the tank:
 *   damage   each present creature's melee per second on the tank (live damage fields, after
 *            the tank's armor and its miss / dodge / parry: Value::MeleeTaken; 2026-11-17,
 *            fight-20261116-230431: without avoidance a bear took 425/s against 679 forecast),
 *            or its main ranged spell (WorldModelBuilder::RangedSpellDps) when that is more,
 *            x DAMAGE_MARGIN (other spells, crushing blows, a slow kill).
 *            A creature with arriveSec > 0 (a patrol coming later) hits from then on.
 *   kills    the party's damage per second (Value::DealtDps: this fight, else the last one)
 *            goes to the present creature with the most damage per remaining health first.
 *   healing  healer bots (PartyMemberFacts::healRates, Perception/Forecast/HealCapacity.h: real spells,
 *            +healing gear) cover the damage and refill the tank's deficit over TOP_UP_SEC.
 *            The heal is picked by the bots' own rule (HealSpells::Choose): the cheapest that
 *            keeps up with the damage less the health above HealSpells::SAFE_FLOOR_PCT spread
 *            over HORIZON_SEC (a healthy tank gets Flash of Light and sinks slowly toward the
 *            floor; until 2026-11-17 it had to cover all the damage, so Holy Light at a third
 *            of the mana efficiency), the fastest in an emergency. Mana for the heal plus
 *            ManaBudget::OVERHEAL waste, regenerating at its in-combat rate. Not counted:
 *            Illumination, potions, Lay on Hands.
 *            Player healers are not counted (their spells are not known).
 *   spawns   (todo 9) a creature's death spawns come when it dies, its aggro summons with it
 *            (Build, FromWorld; CreatureProfile::deathSpawns / aggroSpawns, health and melee
 *            from the template); a self-destruct blows up on the tank lifeSec after it came
 *            unless the party killed it first (Broken Cadaver).
 *   off-tanks (Input::holders, user 2026-11-15: the hunter's pet holds one enemy) a foe with a
 *            holder hits it instead of the tank (holderDps: after the holder's armor). The
 *            healers split their heals by each one's need. A holder that drops below
 *            HOLDER_GIVE_UP_PCT gives its foes up: they hit the tank from then on (the doctrine's
 *            drop, Coordination/Doctrine/OffTank.h). Damage counts on every holder.
 * The fight ends when no present creature is left; one that arrives after that is another
 * fight, unless it is marked sameFight (a controlled enemy waking up: the gap passes with
 * healing and regeneration, no eating). Bearable: the tank survives and never drops below
 * TANK_FLOOR_PCT.
 *
 * Simulate / Bearable / Score / Describe / FromWorld / FoeOf(facts) are pure. Build and
 * FoeOf(Creature) read live objects (map thread, or the world thread while maps are idle).
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
    float const HOLDER_GIVE_UP_PCT = 25.0f;

    // An off-tank besides the main tank (header "off-tanks").
    struct Holder
    {
        ObjectGuid guid;
        float health = 0.0f;
        float maxHealth = 1.0f;
    };

    struct Foe
    {
        ObjectGuid guid;
        float health = 0.0f;
        float dps = 0.0f;           // on the tank, after armor and avoidance, before DAMAGE_MARGIN
        float arriveSec = 0.0f;     // joins the fight this many seconds after the start
        // Part of this fight even if it arrives after the others died: a controlled enemy that
        // wakes later (the party is still in combat: no eating or drinking in between).
        bool sameFight = false;
        // A death spawn (CreatureProfile::deathSpawns, todo 9): it comes arriveSec after the foe
        // with this guid dies (never, if that one lives or is not in the input). Empty = none.
        ObjectGuid parent;
        // A self-destruct (CreatureProfile::selfDestruct): this long after it came it blows up,
        // `burst` on the tank (after armor), and is gone. 0 = lives until killed.
        float lifeSec = 0.0f;
        float burst = 0.0f;
        // Held by Input::holders[holder] (-1: the tank), hitting it for holderDps (after its
        // armor, before DAMAGE_MARGIN); `dps` is still what it does on the tank.
        int holder = -1;
        float holderDps = 0.0f;
    };

    struct Healer
    {
        std::vector<HealRate> rates;
        float mana = 0.0f;
        float maxMana = 0.0f;
        float regenPerSec = 0.0f;   // inside the five second rule
        float restRegenPerSec = 0.0f; // outside it (AfterWait)
    };

    struct Input
    {
        std::vector<Foe> foes;
        float partyDps = 1.0f;
        float tankHealth = 0.0f;
        float tankMaxHealth = 1.0f;
        std::vector<Healer> healers;
        std::vector<Holder> holders;
    };

    struct Result
    {
        float fightSec = 0.0f;
        float damage = 0.0f;        // on the tank and the holders, margin included
        float healed = 0.0f;
        float peakDps = 0.0f;       // highest damage per second on the tank, margin included
        float lowestTankPct = 100.0f;
        float manaStart = 0.0f;     // all healer bots together
        float manaLeft = 0.0f;
        bool tankDies = false;
        bool unfinished = false;    // still fighting after MAX_SEC
        float healthLeftPct = 0.0f; // share of the creatures' health left when it ended
        float lowestHolderPct = 100.0f; // the lowest of the holders (100 without one)
        bool holderGaveUp = false;  // a holder dropped below HOLDER_GIVE_UP_PCT
    };

    // One STEP_SEC step of Run, for the explanation of a recorded run (dev console fightload()).
    struct Step
    {
        float t = 0.0f;
        float incoming = 0.0f;      // per second, margin included
        float healed = 0.0f;        // this step
        float tank = 0.0f;          // health after the hits and the heals
        float mana = 0.0f;          // all healers, after this step
        std::vector<float> health;  // every foe, after the party's damage (same order as Input::foes)
    };

    // `why`: what this run is for ("as is", "controlled", "start +5 s"), for the calculation
    // trace (Trace/Trace.h): with a trace on, the input and the result are recorded under the
    // current scope. Otherwise the same as Run.
    Result Simulate(Input const& in, char const* why = nullptr);
    // The simulation itself, nothing recorded; `steps` (optional) gets every step.
    Result Run(Input const& in, std::vector<Step>* steps);
    bool Bearable(Result const& r);
    // Higher is better: the tank's lowest health percent; a death or an unfinished fight:
    // minus the share of creature health left.
    float Score(Result const& r);
    // 1 if the tank dies or the fight does not end, else how far the tank drops below
    // TANK_FLOOR_PCT (0 above it, 1 at 0%).
    float Risk(Result const& r);
    // What the fight costs the party in points: Points(damage on the tank) + Risk x `penalty`
    // (Value::DeathPenalty of the tank). Compare two runs of the same fight with it.
    float Loss(WorldModel const& world, Result const& r, float penalty);
    // The same fight started `sec` later, the party standing still in combat meanwhile (a
    // controlled enemy sleeping): healers regain mana outside the five second rule
    // (restRegenPerSec), up to their maximum. Nothing else changes.
    Input AfterWait(Input const& in, float sec);
    // "fight ~62 s, tank down to 41%, incoming up to 520/s, healer mana 5400 -> 1200"
    std::string Describe(Result const& r);

    // The fight against `creatures` (live, alive ones) with the party's main tank (Tanks::
    // HitTaker: with nobody to hold, the member hit) and healer bots as they are now. False when
    // there is nobody alive to put it on.
    bool Build(WorldModel const& world, Map* map, std::vector<ObjectGuid> const& creatures, Input& out);
    Foe FoeOf(Creature const* creature, PartyMemberFacts const& tank);
    // The current fight from the facts only (pure): every acting enemy of ours; one under a
    // control effect joins when it ends (sameFight).
    bool FromWorld(WorldModel const& world, Input& out);

    // Who carries the current fight, when it is not the party as it stands (user 2026-11-15,
    // contingencies: the tank or the healer is gone, can someone else carry it?).
    struct Lineup
    {
        ObjectGuid tank;                // empty: the main tank (Tanks::Main)
        // The stand-in's armor once set up (a druid's bear form; Perception/Profiles/StandIn.h).
        float tankArmorMult = 1.0f;
        // Members with PartyMemberFacts::spareHealRates who heal too (hybrids, players); their
        // soloDps leaves the party's damage. A player's heals count PLAYER_HEAL_SHARE: other
        // roles may fail (ai-architecture rule).
        std::vector<ObjectGuid> spareHealers;
        // Dead members with PartyMemberFacts::selfRes that rise now with riseHealth / riseMana
        // (the in-fight rise, Coordination/Situation/Crisis/SelfRiseAssigner.h): `tank` may be
        // one of them; a healer heals with its spareHealRates from the start.
        std::vector<ObjectGuid> risen;
    };
    float const PLAYER_HEAL_SHARE = 0.5f;
    // FromWorld with that lineup. False: the tank is not there, dead or cannot act.
    bool FromWorld(WorldModel const& world, Lineup const& lineup, Input& out);
    // `enemy` held by the off-tank `holder` (header "off-tanks"; the brain's choice, the caller
    // passes it). False: the enemy is not in the input or not in the facts.
    bool PutOn(WorldModel const& world, Input& in, ObjectGuid enemy, PartyMemberFacts const& holder);
    Foe FoeOf(EnemyFacts const& enemy, PartyMemberFacts const& tank);
    // An idle creature joining later (fear add risk, ControlValue).
    Foe FoeOf(PackCreature const& creature, PartyMemberFacts const& tank, float arriveSec);
}
}

#endif
