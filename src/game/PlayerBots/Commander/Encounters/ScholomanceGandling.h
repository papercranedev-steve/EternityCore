/*
 * PartyBot Commander boss module: Darkmaster Gandling, Scholomance (combat scenario 10).
 *
 * Match: map 289 and Gandling (1853) or a Risen Guardian (11598) among the enemies. Risen
 * Guardians have no world spawns; they only come from the Shadow Portal room events, so one in
 * combat with us means somebody is (or was) fighting in a room. After Gandling dies the module
 * stays active until the last guardian is dead.
 *
 * Phases: none. Gandling uses Shadow Portal (17950) every 20-35 s on a random non-top-threat
 * player in sight; the target's threat is wiped and it is sent to one of six rooms, whose door
 * closes and 3-4 Risen Guardians appear (scenario 10, 1.3).
 *
 * Teleport detection (Refresh, from positions only):
 *   a member moved more than TELEPORT_JUMP_YARDS since the previous refresh and is now within
 *   LANDING_RADIUS of one of the six landing points (scenario 10, 1.4) -> "member in room R".
 *   On the first refresh of a fight (no previous position) being near a landing point is
 *   enough. The mark stays while the member is inside room R (InRoom: same floor, past the
 *   door, not farther from the landing point than the door plus ROOM_MARGIN) and alive.
 *   Guardians are put in a room by the same test (or by the room of the marked member they hit).
 *
 * Plan (AdjustPlan):
 *   - A note for the leader when a healer is teleported (once per teleport).
 *   - Intercepts on guardians are dropped unless the interceptor is in the guardian's room:
 *     FocusAssigner intercepts "summon on a non-tank bot" by distance only, so a ranged bot in
 *     the hall within 40 yd of a guardian behind a closed door would be sent after it.
 *   - While Gandling is alive and acting, a party focus on a guardian is moved back to him.
 *
 * Proposals:
 *   Teleported bot (not healer), Urgent, Attack the closest guardian that hits it or is in its
 *     room, unless its target already is one. Its current target is Gandling behind a closed
 *     door, which never becomes invalid, so the original rotation would keep chasing him.
 *     benefit = Value::HitLoss of that guardian on the bot for GUARD_HORIZON_SEC (at least
 *     MIN_GUARD_POINTS), cost = 0 (the current target is out of reach: nothing is given up).
 *   Nothing once the room has no live guardian: the bot waits for the door to open and the
 *   original follow to bring it back (see limits).
 *
 * Not done here: the main room tank (ThreatBehavior already skips enemies 40+ yd away or out
 * of sight), Gandling's interrupt immunity (CastValues::CanStop reads the profile's immune mask).
 *
 * Known limits: see the .cpp file comment at the top.
 */

#ifndef MANGOS_COMMANDER_ENCOUNTER_SCHOLOMANCE_GANDLING_H
#define MANGOS_COMMANDER_ENCOUNTER_SCHOLOMANCE_GANDLING_H

#include "Commander/Encounters/IEncounter.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/WorldModel.h"
#include "ObjectGuid.h"
#include <string>
#include <vector>

namespace Commander
{
    class ScholomanceGandling : public IEncounter
    {
    public:
        static constexpr uint32 MAP_ID = 289;
        static constexpr uint32 NPC_GANDLING = 1853;
        static constexpr uint32 NPC_RISEN_GUARDIAN = 11598;
        static constexpr uint32 ROOM_COUNT = 6;
        static constexpr int32 NO_ROOM = -1;

        // Landing points are 78-95 yd from the hall; a normal refresh step is a few yards.
        static constexpr float TELEPORT_JUMP_YARDS = 40.0f;
        static constexpr float LANDING_RADIUS = 10.0f;
        // Room bound: no farther from the landing point than the door is, plus this.
        static constexpr float ROOM_MARGIN = 10.0f;
        // Upper rooms are at z ~85-89, lower rooms and the hall at z ~71-76.
        static constexpr float UPPER_FLOOR_Z = 81.0f;

        static constexpr float GUARD_HORIZON_SEC = 10.0f;
        static constexpr float MIN_GUARD_POINTS = 1.0f;

        struct Room
        {
            char const* name;
            Position3 landing;      // spell_target_position of the teleport spell
            Position3 door;         // gameobject position of the room's gate
            uint32 doorGuid;        // world DB gameobject guid (scenario 10, 1.4)
            bool upper;
        };

        static Room const ROOMS[ROOM_COUNT];

        char const* GetId() const override { return "gandling"; }
        bool Matches(WorldModel const& world, uint32 mapId) const override;
        void Refresh(TeamBotAI* bot, WorldModel const& world) override;
        void AdjustPlan(WorldModel const& world, TeamPlan& plan) override;
        void Propose(TeamBotAI const* bot, WorldModel const& world,
                     TeamPlan const& plan, std::vector<Proposal>& out) const override;
        void OnReset() override;

        // Room `pos` is in, NO_ROOM for the hall / corridors.
        static int32 RoomOf(Position3 const& pos);
        static bool InRoom(Position3 const& pos, uint32 room);
        // Landing point within LANDING_RADIUS of `pos`, NO_ROOM if none.
        static int32 LandingNear(Position3 const& pos);

        // Room a member was teleported to and is still in, NO_ROOM if none.
        int32 TeleportedRoom(ObjectGuid member) const;

    private:
        struct LastPos
        {
            ObjectGuid guid;
            Position3 pos;
        };
        struct Teleported
        {
            ObjectGuid member;
            uint32 room = 0;
            uint32 sinceMs = 0;
        };
        struct Guardian
        {
            ObjectGuid guid;
            int32 room = NO_ROOM;
        };

        int32 GuardianRoom(ObjectGuid guardian) const;
        bool IsGuardian(ObjectGuid guid) const;

        ObjectGuid m_gandling;
        bool m_gandlingAlive = false;
        std::vector<LastPos> m_lastPos;         // party positions of the previous refresh
        std::vector<Teleported> m_teleported;
        std::vector<Guardian> m_guardians;      // alive, this refresh

        std::vector<std::string> m_pendingNotes;    // flushed into plan.notes by AdjustPlan

        std::vector<VoiceEvent> m_pendingVoice;      // flushed into plan.voice by AdjustPlan (the party voice)
    };
}

#endif
