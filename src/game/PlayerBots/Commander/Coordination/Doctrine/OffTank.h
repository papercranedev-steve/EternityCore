/*
 * PartyBot Commander layer 2 (brain), doctrine step: enemies an off-tank holds for the party
 * besides the main tank (user 2026-11-15: the hunter's pet first, a special off-tank with no
 * taunt, no survival spells, no area threat and less health; later a second tank). Writes
 * DoctrinePlan::offTanks and stamps their enemies STAMP_OFF_TANK. Under the Area tactic too
 * (user: a warrior holds an area pile badly, one enemy less on him helps; the area phase waits
 * for the off-tank's threat on its enemy as for the tank's, AreaPhase TankFill).
 *
 * Holders: members with PartyMemberFacts::canOffTank that can act, only while there is a
 * main tank (without one a pet with its taunt on autocast holds everything it is hit by,
 * ThreatBehavior). One enemy per holder (no area threat to keep more).
 *
 * Kept (from the last refresh): while the enemy is alive, acting and not controlled, the holder
 * can act, is not in danger (SurvivalValue::Of: it gives the enemy up, the main tank's rescue
 * takes it, "other roles may fail" works both ways) and the enemy hit it within LOST_MS. A
 * handed one not taken yet: until its takeByMs.
 *
 * Taken: an enemy hitting a free holder (it won the threat: the brain's rescue gave it the
 * enemy, RescueAssigner, or it got it on its own), when the holder has at least ADOPT_MIN_PCT
 * health and is not in danger, and the enemy is one an off-tank may hold (MayTake: not one the
 * party must kill first or need not hold, KillNow, KillOrder, FreeAlly, SelfDestruct, Swarm,
 * NoHold, HoldFire, not a player's target, not the heavy enemy of a pile), and not the main
 * tank's own (its victim, the pull target), unless the rescue gave it to the holder.
 *
 * Handed (the tank holds too much, user 2026-11-15): a free holder and enemies on the main tank
 * it may take. For each, the fight now (FightLoad::FromWorld with the kept ones on their
 * holders, PutHeld) as is and with that one on the holder (FightLoad::PutOn): saves = Loss as
 * is - Loss handed (penalty: the main tank's DeathPenalty). The holder's lower armor makes
 * every hit bigger, so it only pays when the tank's risk goes down: the tank under pressure.
 * That is `load`. Threat split (many enemies, the tank cannot keep threat on all): the tank's
 * threat for the horizon (ThreatGap::FillRate x DoctrinePlan::RunwayHorizon) shared among the
 * enemies it keeps (ThreatRisk::Counts, not handed), ThreatRisk::Of each with its share (and
 * the area threat the doctrine counts on); handed: one fewer to share among, plus the handed
 * one's risk on the holder once it has 110% of the tank's threat on it. split = sum before -
 * sum after. Rough: an area threat ability does not split (Thunder Clap hits them all), so a
 * warrior's gain is overstated. saves = load + split.
 * Only one the holder can win within TAKE_MAX_SEC (110% of the tank's threat on it, at the
 * holder's threat rate, plus the walk) and that lives HOLD_MIN_SEC longer. The best above
 * MIN_GAIN is handed: the tank leaves it (TankTargets), the holder goes for it (ThreatBehavior
 * ProposePet), takeByMs = now + its win time + TAKE_MARGIN_SEC.
 *
 * Crowd control comes after this step (TacticCheck: the fight with the off-tanks' enemies on
 * them; an off-tank's enemy is not a hold candidate).
 * Pure (facts and plan; plan.rescues: the last refresh's).
 */

#ifndef MANGOS_COMMANDER_OFF_TANK_H
#define MANGOS_COMMANDER_OFF_TANK_H

#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/Forecast/FightLoad.h"

namespace Commander
{
namespace OffTank
{
    // Untested guesses.
    uint32 const LOST_MS = 3000;
    float const ADOPT_MIN_PCT = 60.0f;
    float const TAKE_MAX_SEC = 6.0f;
    float const TAKE_MARGIN_SEC = 2.0f;
    float const HOLD_MIN_SEC = 8.0f;
    float const MIN_GAIN = 1.0f;
    float const RUN_YARDS_PER_SEC = 7.0f;
    float const MELEE_REACH = 5.0f;
    float const TANK_REACH = 40.0f;         // = RescueAssigner::TANK_REACH

    // `m` can hold an enemy as an off-tank now (header "Holders").
    bool IsHolder(WorldModel const& world, PartyMemberFacts const& m);
    // `m` holds nothing yet and is fit to take an enemy (header "Taken": health, danger).
    bool IsFree(WorldModel const& world, DoctrinePlan const& d, PartyMemberFacts const& m);
    // An off-tank may hold `e` (header "Taken": its stamps, not a pile's heavy enemy). The main
    // tank's own enemy is the caller's check.
    bool MayTake(DoctrinePlan const& d, EnemyFacts const& e);
    // The off-tanks' enemies on their holders in a fight run (FightLoad::PutOn).
    void PutHeld(WorldModel const& world, DoctrinePlan const& d, FightLoad::Input& fight);

    // `plan`: read (rescues, pull), and a party note when one is handed over.
    void Apply(WorldModel const& world, TeamPlan& plan, DoctrinePlan const& prev, DoctrinePlan& d);
}
}

#endif
