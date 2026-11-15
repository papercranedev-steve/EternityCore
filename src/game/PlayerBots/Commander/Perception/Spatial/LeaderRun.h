/*
 * PartyBot Commander layer 1 (eyes): is the leader running away from the fight? (user
 * 2026-11-15: "read the player": a player running away starts the retreat, RunAwayAssigner.)
 *
 * Per refresh, while the party fights (WorldModel::HasPartyEnemy), from the leader's facts:
 *   a step    the leader moved at least MIN_STEP_YD since the last refresh, is not casting,
 *             and the step does not go back towards the fight (cos >= MIN_AWAY_COS against
 *             the run's way out).
 *   the run   starts with such a step: from where the leader stood, the way out = from the
 *             fight's centre then to him (the main tank when it is not the leader, else the
 *             enemies not on him, else all enemies; standing on it: the step's direction).
 *             gainYd = how far he got along the way out (a turn in a corridor costs a little).
 *             It ends when no step came for GRACE_MS, or he came back below half his best gain.
 *   off map   the leader is not on this map: onMap false; leftInFight when he was here while
 *             the party fought (he went out through a dungeon door), kept until the fight ends.
 * Facts only, no decision (the thresholds are the brain's). Pure on the facts; state across
 * refreshes, one per PartyCommander.
 */

#ifndef MANGOS_COMMANDER_LEADER_RUN_H
#define MANGOS_COMMANDER_LEADER_RUN_H

#include "Commander/Perception/WorldModel.h"

namespace Commander
{
    class LeaderRun
    {
    public:
        static constexpr float MIN_STEP_YD = 0.3f;
        static constexpr float MIN_AWAY_COS = 0.0f;
        static constexpr uint32 GRACE_MS = 800;

        void Annotate(WorldModel& world, uint32 nowMs);

    private:
        void Reset();

        bool m_hasLast = false;
        Position3 m_last;
        bool m_running = false;
        uint32 m_startMs = 0;
        uint32 m_lastStepMs = 0;
        Position3 m_start;
        float m_outX = 0.0f, m_outY = 0.0f;     // the way out, unit vector
        float m_bestGain = 0.0f;
        bool m_wasHereInFight = false;
        bool m_leftInFight = false;
    };
}

#endif
