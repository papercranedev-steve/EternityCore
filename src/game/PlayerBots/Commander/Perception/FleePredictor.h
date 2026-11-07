/*
 * PartyBot Commander layer 1 (eyes): which enemies are about to run away at low health, and
 * what it costs the party if they do (todo 12, .kiro/docs/2026-11-08-flee-predictor.md).
 * Facts only: who stops the run is the brain's (todo 13).
 *
 * Server rules (code reading, 2026-11-08):
 *  - Who: creature_ai_events EVENT_T_HP + SCRIPT_COMMAND_FLEE (CreatureProfile::fleeAtHealthPct).
 *    Every one in the dungeons is "flee at 15%", not repeatable (once per life; an evade resets
 *    it), not while casting. No generic humanoid flee. C++ scripted bosses (Herod, Timmy) are
 *    left to their boss modules. Seek-assistance (datalong 1, run to the nearest friend) is
 *    used by one outdoor creature only: not modelled.
 *  - Where: Creature::DoFlee -> TimedFleeingMovementGenerator, CreatureFamilyFleeDelay (7 s).
 *    Legs in random directions (30 yd path limit, 0.8-1.5 s pause), until 28-38 yd from its
 *    victim; then it attacks the same victim again. A stun pauses the run and its timer.
 *  - Who comes: every update while running, Creature::CallForHelp(10): idle creatures within
 *    10 yd in sight that are friendly to it (any faction, not only its own) attack its victim at
 *    once. They start attacking, so they call assistance and bring their groups.
 *  - Prevented by SPELL_AURA_PREVENTS_FLEEING (Curse of Recklessness, Judgement of Justice).
 *
 * Worst case (user, 2026-11-08): every creature the run can come near joins.
 *   reach    min(run speed x flee time, MAX_RUN_YD) from where it stands now
 *   seeds    idle creatures passing the server's CallForHelp checks within reach + CALL_YD,
 *            in sight from it, or with a navmesh path from it no longer than that
 *   joiners  AssistPredictor::Expand(seeds): their aggro-together groups and the assistance
 *            they call
 *   points   FightLoad: the current fight (FromWorld) once as it is and once with the joiners
 *            arriving secToFlee + JOIN_SEC in (sameFight); Loss(with) - Loss(as is), >= 0.
 *            The runner's own 7 s away is not counted.
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

#include "Commander/Perception/FightLoad.h"
#include "Commander/Perception/WorldModel.h"
#include <vector>

class Map;

namespace Commander
{
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
        static constexpr float MOVED_YD = 4.0f;
        static constexpr uint32 MAX_AGE_MS = 2000;
        static constexpr uint32 MAX_SEARCHES_PER_REFRESH = 3;
        static constexpr uint32 MAX_PATHS_PER_ENEMY = 8;

        // Fills EnemyFacts::flee of world.enemies.
        void Annotate(Map* map, WorldModel& world, uint32 nowMs);

    private:
        struct Entry
        {
            ObjectGuid enemy;
            Position3 at;
            uint32 atMs = 0;
            std::vector<ObjectGuid> joiners;
            std::vector<FightLoad::Foe> foes;   // the joiners on the tank, arriveSec 0
        };
        std::vector<Entry> m_cache;
        std::vector<ObjectGuid> m_fled;         // seen running, alive and still on our lists
    };
}

#endif
