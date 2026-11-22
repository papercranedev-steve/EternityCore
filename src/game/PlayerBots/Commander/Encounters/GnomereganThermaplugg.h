/*
 * PartyBot Commander boss module: Mekgineer Thermaplugg, Gnomeregan (combat scenario 8).
 *
 * Match: map 90 and Thermaplugg (7800) or a Walking Bomb (7915) among the enemies. Bombs
 * outlive the boss (TEMPSUMMON_CORPSE_DESPAWN, JustDied only clears the script's list), so
 * the module stays active until the last bomb in combat with us is dead.
 *
 * Phases (from world.enemies on each refresh):
 *   PhaseOne  - boss alive, >= 50%: single-target Knock Away (clears the victim's threat).
 *   PhaseTwo  - boss alive, < 50%: area Knock Away, faces activated faster.
 *   BossDead  - boss gone, bombs still in combat.
 *
 * Gnome faces (scenario 8, 1.4): Refresh reads the six face gameobjects. Lit (spawning bombs)
 * = GO_STATE_ACTIVE, dark = GO_STATE_READY; why, see FaceIsLit in the .cpp.
 *
 * Plan (AdjustPlan):
 *   - Every bomb in world.enemies gets an intercept. FocusAssigner already intercepts
 *     self-destructing enemies; this only adds the bombs it left out (ranged first, then melee
 *     close to the bomb, never the button presser).
 *
 * Proposals:
 *   Button presser (one melee damage dealer, else one ranged; picked in Refresh at
 *     >= PRESSER_MIN_HEALTH_PCT, sticky while alive, on this map and at >=
 *     PRESSER_STAY_HEALTH_PCT, else re-picked), Assigned, UseObject on the button of the lit
 *     face closest to him, while the boss is alive. A button just used is skipped until its
 *     face is dark or BUTTON_REUSE_MS passed; after PRESS_VERIFY_MS a press whose face is still
 *     lit while the presser is not at the button is dropped (the use did not happen).
 *     benefit = BOMBS_PER_FACE_WINDOW x explosion loss (BOMB_VICTIMS x BOMB_DAMAGE, in points),
 *     cost    = Value::LostAction for walking to the button, using it and walking back to the boss.
 *   Melee damage dealers whose target is a bomb they were not assigned to intercept, Assigned:
 *     Attack the boss if alive (benefit = one explosion, cost = a target switch plus the walk),
 *     else HoldFire (benefit = one explosion, cost = one tick of damage). Their swings put them
 *     within the 5 yd trigger range and the blast hits the whole melee pile.
 *   Tank: nothing. After Knock Away the tank's victim is still the boss, so an Attack on the boss
 *     would fail (Arbiter: same victim) and the original chase already walks it back;
 *     the team rescue (RescueAssigner) has it taunted once it is in range (scenario 8, T+17s).
 *
 * Known limits: see the .cpp file comment at the top.
 */

#ifndef MANGOS_COMMANDER_ENCOUNTER_GNOMEREGAN_THERMAPLUGG_H
#define MANGOS_COMMANDER_ENCOUNTER_GNOMEREGAN_THERMAPLUGG_H

#include "Commander/Encounters/IEncounter.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/WorldModel.h"
#include "ObjectGuid.h"
#include <string>

namespace Commander
{
    class GnomereganThermaplugg : public IEncounter
    {
    public:
        static constexpr uint32 MAP_ID = 90;
        static constexpr uint32 NPC_THERMAPLUGG = 7800;
        static constexpr uint32 NPC_WALKING_BOMB = 7915;
        static constexpr uint32 FACE_COUNT = 6;     // MAX_GNOME_FACES (gnomeregan.h)

        static constexpr float PHASE_TWO_PCT = 50.0f;

        // Value of switching one face off: bombs it would drop in the next FACE_WINDOW_SEC
        // (first one 3 s after lighting up, then every 10-25 s: about 2 in 30 s).
        static constexpr float FACE_WINDOW_SEC = 30.0f;
        static constexpr float BOMBS_PER_FACE_WINDOW = 2.0f;
        // One explosion (spell 11504): 200-300 to everyone within 10 yd, about two of us.
        static constexpr float BOMB_VICTIMS = 2.0f;
        static constexpr float BOMB_DAMAGE = 250.0f;

        // Hysteresis: a new presser needs PRESSER_MIN_HEALTH_PCT, the current one keeps the job
        // down to PRESSER_STAY_HEALTH_PCT. With one threshold two candidates near 50% swapped
        // the job back and forth on every refresh.
        static constexpr float PRESSER_MIN_HEALTH_PCT = 50.0f;
        static constexpr float PRESSER_STAY_HEALTH_PCT = 40.0f;
        static constexpr float RUN_YARDS_PER_SEC = 7.0f;
        static constexpr float USE_SEC = 1.0f;          // stop, use, turn around

        // Faces are ~50 yd and buttons ~40 yd from the boss.
        static constexpr float OBJECT_SEARCH_RADIUS = 80.0f;
        // A grid search that missed some faces / buttons is repeated at most this often.
        static constexpr uint32 OBJECT_SEARCH_RETRY_MS = 10000;
        // A button just used is not proposed again until its face goes dark or this passes.
        static constexpr uint32 BUTTON_REUSE_MS = 5000;
        // A press is checked this long after it was recorded (ReadFaces): face still lit and
        // the presser not at the button = the use never happened (the Arbiter picked another
        // proposal that tick), the button may be chosen again before BUTTON_REUSE_MS.
        static constexpr uint32 PRESS_VERIFY_MS = 1500;

        enum class Phase : uint8
        {
            None,
            PhaseOne,
            PhaseTwo,
            BossDead,
        };

        char const* GetId() const override { return "thermaplugg"; }
        bool Matches(WorldModel const& world, uint32 mapId) const override;
        void Refresh(TeamBotAI* bot, WorldModel const& world) override;
        void AdjustPlan(WorldModel const& world, TeamPlan& plan) override;
        void Propose(TeamBotAI const* bot, WorldModel const& world,
                     TeamPlan const& plan, std::vector<Proposal>& out) const override;
        void OnReset() override;

        Phase GetPhase() const { return m_phase; }

    private:
        struct Face
        {
            ObjectGuid face;
            ObjectGuid button;
            Position3 buttonPos;
            bool lit = false;
            // Set by ProposePresser (const) on the tick the presser is in reach of the button,
            // cleared by ReadFaces once the face is dark, or after PRESS_VERIFY_MS while still
            // lit with the presser away from the button. Same map thread for the whole party.
            mutable bool pressed = false;
            mutable uint32 pressedAtMs = 0;
        };

        void FindObjects(TeamBotAI* bot, uint32 nowMs);
        void ReadFaces(TeamBotAI* bot);
        // The presser is within INTERACTION_DISTANCE of the face's button.
        bool PresserAtButton(TeamBotAI* bot, Face const& f) const;
        void PickPresser(WorldModel const& world);
        void AddBombIntercepts(WorldModel const& world, TeamPlan& plan) const;

        void ProposePresser(TeamBotAI const* bot, WorldModel const& world, std::vector<Proposal>& out) const;
        void ProposeMelee(TeamBotAI const* bot, WorldModel const& world, TeamPlan const& plan,
                          std::vector<Proposal>& out) const;
        // Points lost to one explosion.
        float ExplosionLoss(WorldModel const& world) const;

        Phase m_phase = Phase::None;
        ObjectGuid m_boss;
        bool m_bossAlive = false;
        float m_bossPct = 100.0f;
        Position3 m_bossPos;
        uint32 m_bombCount = 0;

        Face m_faces[FACE_COUNT];
        bool m_objectsFound = false;
        bool m_searched = false;        // a grid search ran this attempt (m_lastSearchMs valid)
        uint32 m_lastSearchMs = 0;
        uint32 m_litCount = 0;

        ObjectGuid m_presser;
        bool m_presserNoted = false;

        std::vector<std::string> m_pendingNotes;    // flushed into plan.notes by AdjustPlan

        std::vector<VoiceEvent> m_pendingVoice;      // flushed into plan.voice by AdjustPlan (the party voice)
    };
}

#endif
