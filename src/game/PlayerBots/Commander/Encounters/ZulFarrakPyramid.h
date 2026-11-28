/*
 * PartyBot Commander boss module: Zul'Farrak pyramid stairs event (combat scenario 9).
 *
 * Match: map 209 and either
 *   - the pyramid phase read in the last Refresh is a wave phase (WAVE_1 .. WAVE_3), or
 *   - a summoned wave creature (WAVE entries below) is among the enemies. Wave adds come from
 *     Map::SummonCreature (a TemporarySummon), so world spawns of the same entries elsewhere
 *     in the instance do not match.
 * The first match comes from the enemies (wave 1 adds fighting the prisoners are enemies
 * through WorldModel::allies, onAllyOnly). From then on the phase keeps the module active
 * through the gaps when nobody is fighting, until the script reaches KILLED_ALL_TROLLS.
 *
 * Phases (Refresh): instance data GetData(EVENT_PYRAMID). The constants live in the scripts
 * library (zulfarrak.h: EVENT_PYRAMID = 1, enum zfPyramidPhases), which the game library cannot
 * include, so the values are repeated here. instance_zulfarrak.cpp Update():
 *   WAVE_1 / WAVE_2   23 / 24 adds at the bottom, groups of 2, 3, 4, ... sent up every 10 s
 *                     whether the previous group is dead or not.
 *   PRE_WAVE_2        all of wave 1 dead; wave 2 spawns 10 s later (+~7 s run up the stairs).
 *   PRE_WAVE_3        all of wave 2 dead; 7 adds (two elite bosses) spawn at the bottom and
 *                     stay there; 5 s later the prisoners walk down (WAVE_3).
 * Without instance data (script not loaded) the phase is Unknown and only the enemies count.
 *
 * Facts (AddFacts): of the five prisoners only Sergeant Bly (7604) is tankLike. Weegli (7607)
 * has as much health (9.7 x multiplier) but must survive the event (he blows up the door).
 *
 * Doctrine (ShapeDoctrine), waves 1-2 only (wave 3 is left to the generic focus, which scores
 * the Shadowpriest's heals): kill order tiers by scenario 9, 2.1 (DoctrinePlan::SetKillTier),
 * first to last:
 *   hitting a healer bot or a cloth wearer, hitting Weegli, Acolyte (Mana Burn),
 *   Cretin (elite, Shadow Word: Pain), anything else (the role stamps' tier), hitting Bly.
 * TargetAssigner chooses the focus within the highest tier by its points (sticky; fresh and
 * intercepted enemies skipped as for any focus). One note for the leader when wave 3 is
 * coming ("go down with them").
 *
 * Proposals:
 *   Melee dps and tank, waves 1-2: their target is fleeing (UNIT_STATE_FLEEING or below
 *     FLEE_PCT) and more than HOLD_RADIUS from HOLD_POINT -> Assigned MoveTo back to the hold
 *     point (own position once there), so the original chase does not run down the stairs into
 *     the adds waiting at the bottom. Ranged bots keep shooting it (nothing to propose).
 *     benefit = HOLD_POINTS, cost = one tick of the bot's contribution.
 *   Healer: Weegli below WEEGLI_HEAL_PCT, no party member below MEMBERS_FIRST_PCT -> Assigned
 *     Cast of the smallest direct heal that CanTryToCastSpell allows (smallest = least heal
 *     threat; it is split over every enemy on Weegli's list). benefit = WEEGLI_WEIGHT x points
 *     of the healing, cost = cast time of the healer's contribution.
 *
 * Eating and drinking (ForbidsRest, computed in Refresh; TeamPlan::noRest):
 *   WAVE_1 / WAVE_2: never, unless nothing fought for QUIET_STALE_MS (a wipe leaves the phase
 *     stuck; the party then has to drink again).
 *   PRE_WAVE_2: only while the next contact (gap left + WAVE_TRAVEL_SEC) is at least
 *     NO_REST_BEFORE_CONTACT_SEC away, and nothing is fighting.
 *   PRE_WAVE_3 / WAVE_3: drink until somebody (party or prisoners) fights wave 3.
 *
 * Known limits: see the .cpp file comment at the top.
 */

#ifndef MANGOS_COMMANDER_ENCOUNTER_ZULFARRAK_PYRAMID_H
#define MANGOS_COMMANDER_ENCOUNTER_ZULFARRAK_PYRAMID_H

#include "Commander/Encounters/IEncounter.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/WorldModel.h"
#include "ObjectGuid.h"
#include <string>
#include <vector>

namespace Commander
{
    class ZulFarrakPyramid : public IEncounter
    {
    public:
        static constexpr uint32 MAP_ID = 209;

        // zulfarrak.h: EVENT_PYRAMID and enum zfPyramidPhases (scripts library).
        static constexpr uint32 DATA_PYRAMID = 1;
        enum class Phase : uint32
        {
            NotStarted = 0,
            CagesOpen = 1,
            ArrivedAtStair = 2,
            Wave1 = 3,
            PreWave2 = 4,
            Wave2 = 5,
            PreWave3 = 6,
            Wave3 = 7,
            KilledAll = 8,
            Unknown = 0xFFFF,   // no instance data: only the enemies tell
        };

        // Prisoners (zulfarrak.h zfEntries).
        static constexpr uint32 NPC_BLY = 7604;
        static constexpr uint32 NPC_RAVEN = 7605;
        static constexpr uint32 NPC_ORO = 7606;
        static constexpr uint32 NPC_WEEGLI = 7607;
        static constexpr uint32 NPC_MURTA = 7608;
        // Wave adds (instance_zulfarrak.cpp pyramidSpawns).
        static constexpr uint32 NPC_SLAVE = 7787;
        static constexpr uint32 NPC_DRUDGE = 7788;
        static constexpr uint32 NPC_CRETIN = 7789;
        static constexpr uint32 NPC_ACOLYTE = 8876;
        static constexpr uint32 NPC_ZEALOT = 8877;
        static constexpr uint32 NPC_SEZZZIZ = 7275;
        static constexpr uint32 NPC_NEKRUM = 7796;

        // Top of the stairs, behind the prisoners' spot (scenario 9, 1.1). The stairs run south
        // (y down) to the spawn area at y 1202-1226, z 8.9; 20 yd keeps a bot 25+ yd from it.
        static Position3 const HOLD_POINT;
        static constexpr float HOLD_RADIUS = 20.0f;
        static constexpr float HOLD_ARRIVE_YARDS = 3.0f;
        static constexpr uint32 HOLD_MOVE_MS = 1500;
        static constexpr float FLEE_PCT = 15.0f;            // Slave, Acolyte, Cretin flee at 15%
        // Not running down the stairs: a pull of up to a whole group of waiting adds.
        static constexpr float HOLD_POINTS = 5.0f;

        static constexpr float WEEGLI_HEAL_PCT = 40.0f;
        static constexpr float MEMBERS_FIRST_PCT = 50.0f;
        static constexpr float WEEGLI_WEIGHT = 2.0f;        // the event needs him alive

        static constexpr uint32 PRE_WAVE_2_MS = 10000;      // script major_wave_Timer
        static constexpr float WAVE_TRAVEL_SEC = 7.0f;      // ~60 yd run at speed 1.14
        static constexpr float NO_REST_BEFORE_CONTACT_SEC = 8.0f;
        static constexpr uint32 QUIET_STALE_MS = 25000;     // groups come every 10 s, ~8 s run

        char const* GetId() const override { return "zf-pyramid"; }
        bool Matches(WorldModel const& world, uint32 mapId) const override;
        void Refresh(TeamBotAI* bot, WorldModel const& world) override;
        void AddFacts(WorldModel& world) override;
        void ShapeDoctrine(WorldModel const& world, TeamPlan& plan) override;
        void Propose(TeamBotAI const* bot, WorldModel const& world,
                     TeamPlan const& plan, std::vector<Proposal>& out) const override;
        bool ForbidsRest() const override { return m_noRest; }
        void OnReset() override;

        static bool IsWaveEntry(uint32 entry);
        static bool IsPrisonerEntry(uint32 entry);
        static bool IsWavePhase(Phase phase);

        Phase GetPhase() const { return m_phase; }
        // Waves not finished yet, the current one included (0 = event over / unknown).
        uint32 GetWavesLeft() const { return m_wavesLeft; }
        // Between two waves (PreWave2, PreWave3).
        bool InGap() const { return m_inGap; }

    private:
        bool HoldsTheStairs() const;    // waves 1-2 (and Unknown): the party fights at the top

        Phase m_phase = Phase::NotStarted;
        uint32 m_phaseSinceMs = 0;
        uint32 m_lastFightMs = 0;       // last refresh with anyone (party or ally) fighting
        uint32 m_wavesLeft = 0;
        bool m_inGap = false;
        bool m_noRest = false;
        bool m_wave3Noted = false;
    };
}

#endif
