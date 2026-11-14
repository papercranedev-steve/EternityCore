/*
 * PartyBot Commander layer 1 (eyes): which enemies are about to run away at low health, and
 * what it costs the party if they do (todo 12, .kiro/docs/2026-11-08-flee-predictor.md).
 * Facts only: who stops the run is the brain's (todo 13, FleeStopAssigner).
 *
 * Server rules (code reading, 2026-11-08):
 *  - Who: creature_ai_events EVENT_T_HP + SCRIPT_COMMAND_FLEE (CreatureProfile::fleeAtHealthPct).
 *    Every one in the dungeons is "flee at 15%", not repeatable (once per life; an evade resets
 *    it), not while casting. No generic humanoid flee. C++ scripted bosses (Herod, Timmy) are
 *    left to their boss modules. Seek-assistance (datalong 1, run to the nearest friend) is
 *    used by one outdoor creature only: not modelled.
 *  - Where: Creature::DoFlee -> TimedFleeingMovementGenerator, CreatureFamilyFleeDelay (7 s).
 *    Legs in random directions (30 yd path limit, 0.8-1.5 s pause), until 28-38 yd from its
 *    victim; then it attacks the same victim again. A stun or root pauses the run and its
 *    timer (it does not end it).
 *  - Who comes: every update while running, Creature::CallForHelp(10): idle creatures within
 *    10 yd in sight that are friendly to it (any faction, not only its own) attack its victim at
 *    once. They start attacking, so they call assistance and bring their groups.
 *  - Prevented by SPELL_AURA_PREVENTS_FLEEING (Curse of Recklessness, Judgement of Justice).
 *
 * Worst case (user, 2026-11-08): every creature the run can come near joins.
 *   reach    min(run speed x flee time, MAX_RUN_YD) from where it stands now
 *   callers  idle creatures passing the server's CallForHelp checks within reach + CALL_YD,
 *            in sight from it, or with a navmesh path from it no longer than that
 *   joiners  each caller with what it brings (AssistPredictor::Expand: aggro-together groups,
 *            assistance), FleeJoiner::needYd = the caller's distance - CALL_YD
 *   points   PointsWithin(reach): FightLoad, the current fight (FromWorld) once as it is and
 *            once with the joiners within that reach arriving secToFlee + JOIN_SEC in
 *            (sameFight); Loss(with) - Loss(as is), >= 0. The runner's own 7 s away is not
 *            counted.
 * Focused (todo 13, user 2026-11-08: the usual answer is to have the party on it in time):
 *   runKillSec   (health when it starts to run - melee share x MELEE_CONTACT_SEC) / ranged
 *                share of Value::DealtDps: melee are as fast as it is and lose it, ranged keep
 *                shooting; the whole flee time when nobody ranged is there
 *   focusedReachYd  min(reach, run speed x runKillSec); focusedPoints = PointsWithin(it)
 * secToFlee = Value::EnemyTimeToDie x (health% - atPct) / health%.
 *
 * Cost: one grid search and up to MAX_PATHS_PER_ENEMY navmesh queries per predicted enemy,
 * at most MAX_SEARCHES_PER_REFRESH searches per refresh; joiners are kept until the enemy
 * moved MOVED_YD or MAX_AGE_MS passed. Points are recomputed every refresh (pure).
 *
 * Map thread (PartyCommander::Refresh, after RefreshCombat): reads creatures and pathfinds.
 */

#ifndef MANGOS_COMMANDER_FLEE_PREDICTOR_H
#define MANGOS_COMMANDER_FLEE_PREDICTOR_H

#include "Commander/Perception/WorldModel.h"
#include "Commander/Perception/Forecast/FightLoad.h"
#include <vector>

class Map;

namespace Commander
{
    struct TeamPlan;

    class FleePredictor
    {
    public:
        // Predicted from this far above the flee line (FocusAssigner's kill bonus uses the same window).
        static constexpr float WINDOW_PCT = 10.0f;
        // First leg (30 yd path limit) plus one more leg of the 7 s.
        static constexpr float MAX_RUN_YD = 42.0f;
        static constexpr float CALL_YD = 10.0f;         // FleeingMovementGenerator: CallForHelp(10)
        // Joiners come while it runs, not when it starts: about the first leg. Untested guess.
        static constexpr float JOIN_SEC = 2.0f;
        // Melee still hit it this long after it turns (the first steps). Untested guess.
        static constexpr float MELEE_CONTACT_SEC = 1.0f;
        static constexpr float MOVED_YD = 4.0f;
        static constexpr uint32 MAX_AGE_MS = 2000;
        static constexpr uint32 MAX_SEARCHES_PER_REFRESH = 3;
        static constexpr uint32 MAX_PATHS_PER_ENEMY = 8;

        // Fills EnemyFacts::flee of world.enemies.
        void Annotate(Map* map, WorldModel& world, uint32 nowMs);

        // Before the fight of a pull fought "here" (tank fight spot doc 3.5): world.pullRunners,
        // the pull's creatures that run for help, each with its joiners searched from where it
        // stands FIGHT_SPOT_YD farther than its run (the fight spot may take the pile that far:
        // FightSpot::MAX_YD) and with the party's run kill time (RunKillSec at its flee line,
        // full health before: arriveSec as if it were killed first). Once per planned pull.
        static constexpr float FIGHT_SPOT_YD = 35.0f;
        void AnnotatePull(Map* map, WorldModel& world, TeamPlan const& plan);

        // A run starting at `start` (the fight spot's candidate; straight line to each joiner's
        // caller - CALL_YD within `reachYd`): bit i = joiners[i] comes (at most 64).
        static uint64 Reached(std::vector<FleeJoiner> const& joiners, Position3 const& start, float reachYd);
        // Points the joiners of `mask` arriving `arriveSec` in cost on top of `base` (its Loss:
        // `baseLoss`), as PointsWithin. Pure.
        static float PointsOf(WorldModel const& world, FightLoad::Input const& base, float baseLoss,
                              std::vector<FleeJoiner> const& joiners, uint64 mask, float arriveSec);

        // Points the joiners of `e` within a run of `reachYd` cost (header). Pure.
        static float PointsWithin(WorldModel const& world, EnemyFacts const& e, float reachYd);
        // Health it has when it starts to run (now if it already runs).
        static float HealthAtRun(EnemyFacts const& e);
        // Seconds the party needs to kill it while it runs (header), FleeFacts::runKillSec.
        static float RunKillSec(WorldModel const& world, EnemyFacts const& e, float fleeSec);
        static float FleeSec();

    private:
        struct Entry
        {
            ObjectGuid enemy;
            Position3 at;
            uint32 atMs = 0;
            std::vector<FleeJoiner> joiners;
        };
        std::vector<Entry> m_cache;
        std::vector<ObjectGuid> m_fled;         // seen running, alive and still on our lists
        uint32 m_pullAtMs = 0;                  // PullPlan::plannedAtMs of m_pullRunners
        std::vector<PullRunnerFacts> m_pullRunners;
    };
}

#endif
