/*
 * Gandling boss module (header comment: match, teleport detection, plan, proposals).
 *
 * Known limits:
 *   - A teleport is only seen on the next refresh (0.2 - 1.2 s), and only from positions.
 *     A member who walks out of a room before the next refresh after landing is not marked.
 *   - Once the room's guardians are dead the bot does nothing special: the door opens (event
 *     end condition), and the original rotation / follow brings it back. While the door is
 *     still closed (a second teleport into a room whose event is running, scenario 10, 1.3)
 *     the bot may walk through it: bot pathing ignores doors (TargetedMovementGenerator
 *     only cuts paths for pets). The door state is not read yet.
 *   - Teleported healers get no proposal: an Attack would turn the healer into a melee; its
 *     rotation heals itself (in sight). The main group gets no help either (no tank cooldowns,
 *     no hybrid healing, scenario 10, T+45s); the leader only gets a note.
 *   - A teleported player (the leader) is marked but nothing changes for the bots: following
 *     the leader in combat is official PartyBot code (scenario 10, 3.5).
 *   - Room bounds are a half-plane at the door plus a distance limit, on two floors split at
 *     UPPER_FLOOR_Z; stairs and corridors near the doors are guesses, not verified in game.
 *   - Rates (CombatRates) still mix the hall and the rooms into one fight.
 */

#include "Commander/Encounters/ScholomanceGandling.h"
#include "Commander/Encounters/EncounterHelpers.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/Spatial/PackResolver.h"
#include "Commander/Perception/Values/ValueModel.h"
#include "PartyBotAI.h"
#include "Map.h"
#include "Player.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace Commander
{
    // Scenario 10, 1.4: landing points (spell_target_position of 17863 / 17939 / 17943 /
    // 17944 / 17946 / 17948) and gates (world DB `gameobject`, entries 177371-177377).
    ScholomanceGandling::Room const ScholomanceGandling::ROOMS[ScholomanceGandling::ROOM_COUNT] =
    {
        { "Polkelt",   { 274.9f,    1.3f, 85.3f }, { 209.065f,   0.011f, 88.911f }, 43196, true  },
        { "Krastinov", { 182.2f,  -95.4f, 85.3f }, { 181.244f, -29.112f, 88.910f }, 43203, true  },
        { "Malicia",   {  86.7f,   -2.0f, 85.3f }, { 151.773f,  -0.602f, 88.904f }, 43184, true  },
        { "Illucia",   { 266.0f,    0.9f, 75.3f }, { 204.326f,   0.011f, 76.168f }, 35798, false },
        { "Alexei",    { 178.7f,  -91.0f, 70.9f }, { 181.054f, -21.347f, 76.132f }, 43188, false },
        { "Ravenian",  { 103.3f,   -1.7f, 75.2f }, { 159.746f,  -0.767f, 76.168f }, 43187, false },
    };

    namespace
    {
        char const* const SOURCE = "gandling";

        float Distance2d(Position3 const& a, Position3 const& b)
        {
            float const dx = a.x - b.x;
            float const dy = a.y - b.y;
            return std::sqrt(dx * dx + dy * dy);
        }
    }

    bool ScholomanceGandling::InRoom(Position3 const& pos, uint32 room)
    {
        if (room >= ROOM_COUNT)
            return false;
        Room const& r = ROOMS[room];
        if ((pos.z > UPPER_FLOOR_Z) != r.upper)
            return false;
        // Past the door, seen from the hall: positive along door -> landing.
        float const dirX = r.landing.x - r.door.x;
        float const dirY = r.landing.y - r.door.y;
        if ((pos.x - r.door.x) * dirX + (pos.y - r.door.y) * dirY <= 0.0f)
            return false;
        return Distance2d(pos, r.landing) <= Distance2d(r.door, r.landing) + ROOM_MARGIN;
    }

    int32 ScholomanceGandling::RoomOf(Position3 const& pos)
    {
        for (uint32 i = 0; i < ROOM_COUNT; ++i)
            if (InRoom(pos, i))
                return int32(i);
        return NO_ROOM;
    }

    int32 ScholomanceGandling::LandingNear(Position3 const& pos)
    {
        for (uint32 i = 0; i < ROOM_COUNT; ++i)
            if (PackResolver::Distance(pos, ROOMS[i].landing) <= LANDING_RADIUS)
                return int32(i);
        return NO_ROOM;
    }

    bool ScholomanceGandling::Matches(WorldModel const& world, uint32 mapId) const
    {
        if (mapId != MAP_ID)
            return false;
        for (EnemyFacts const& e : world.enemies)
            if (e.entry == NPC_GANDLING || e.entry == NPC_RISEN_GUARDIAN)
                return true;
        return false;
    }

    void ScholomanceGandling::OnReset()
    {
        *this = ScholomanceGandling();
    }

    int32 ScholomanceGandling::TeleportedRoom(ObjectGuid member) const
    {
        for (Teleported const& t : m_teleported)
            if (t.member == member)
                return int32(t.room);
        return NO_ROOM;
    }

    int32 ScholomanceGandling::GuardianRoom(ObjectGuid guardian) const
    {
        for (Guardian const& g : m_guardians)
            if (g.guid == guardian)
                return g.room;
        return NO_ROOM;
    }

    bool ScholomanceGandling::IsGuardian(ObjectGuid guid) const
    {
        return std::any_of(m_guardians.begin(), m_guardians.end(),
            [guid](Guardian const& g) { return g.guid == guid; });
    }

    void ScholomanceGandling::Refresh(PartyBotAI* bot, WorldModel const& world)
    {
        uint32 const now = world.refreshedAtMs;

        EnemyFacts const* gandling = EncounterHelpers::FindEnemyByEntry(world, NPC_GANDLING);
        m_gandlingAlive = gandling != nullptr;
        if (gandling)
            m_gandling = gandling->guid;

        // Teleports: big jump onto a landing point.
        std::vector<LastPos> positions;
        for (PartyMemberFacts const& m : world.party)
        {
            if (m.isPet)
                continue;
            positions.push_back({ m.guid, m.pos });
            if (!m.alive)
                continue;

            LastPos const* previous = nullptr;
            for (LastPos const& p : m_lastPos)
                if (p.guid == m.guid)
                    previous = &p;
            bool const jumped = !previous || PackResolver::Distance(previous->pos, m.pos) > TELEPORT_JUMP_YARDS;
            int32 const room = jumped ? LandingNear(m.pos) : NO_ROOM;
            if (room == NO_ROOM)
                continue;

            // New teleport (also into another room, or into the same room a second time).
            m_teleported.erase(std::remove_if(m_teleported.begin(), m_teleported.end(),
                [&m](Teleported const& t) { return t.member == m.guid; }), m_teleported.end());
            Teleported t;
            t.member = m.guid;
            t.room = uint32(room);
            t.sinceMs = now;
            m_teleported.push_back(t);

            if (m.isBot && m.role == ROLE_HEALER)
            {
                Player* healer = bot ? bot->me->GetMap()->GetPlayer(m.guid) : nullptr;
                char buf[200];
                snprintf(buf, sizeof(buf), "Gandling sent healer %s to the %s room: no heals in the hall until the room is cleared.",
                    healer ? healer->GetName() : "?", ROOMS[room].name);
                m_pendingNotes.push_back(buf);
            }
        }
        m_lastPos.swap(positions);

        // Marks last while the member is alive and still inside the room.
        m_teleported.erase(std::remove_if(m_teleported.begin(), m_teleported.end(),
            [&world](Teleported const& t)
            {
                PartyMemberFacts const* m = world.FindMember(t.member);
                return !m || !m->alive || !InRoom(m->pos, t.room);
            }), m_teleported.end());

        // Guardians: by position, else by the room of the marked member they are hitting.
        m_guardians.clear();
        for (EnemyFacts const& e : world.enemies)
        {
            if (e.entry != NPC_RISEN_GUARDIAN)
                continue;
            Guardian g;
            g.guid = e.guid;
            g.room = RoomOf(e.pos);
            if (g.room == NO_ROOM && !e.victim.IsEmpty())
                g.room = TeleportedRoom(e.victim);
            m_guardians.push_back(g);
        }
    }

    void ScholomanceGandling::AdjustPlan(WorldModel const& world, TeamPlan& plan)
    {
        for (std::string& note : m_pendingNotes)
            plan.notes.push_back(std::move(note));
        m_pendingNotes.clear();

        // Guardian intercepts only for someone in the same room.
        plan.intercepts.erase(std::remove_if(plan.intercepts.begin(), plan.intercepts.end(),
            [this](InterceptDuty const& d)
            {
                if (!IsGuardian(d.enemy))
                    return false;
                int32 const room = GuardianRoom(d.enemy);
                return room == NO_ROOM || TeleportedRoom(d.bot) != room;
            }), plan.intercepts.end());

        // The party focus stays on Gandling while he can be fought; guardians are the
        // teleported bot's job (Propose).
        if (m_gandlingAlive && !plan.focus.enemy.IsEmpty() && IsGuardian(plan.focus.enemy))
        {
            EnemyFacts const* gandling = world.FindEnemy(m_gandling);
            if (gandling && gandling->acting)
                EncounterHelpers::SetFocus(world, plan, *gandling, "gandling: guardians are the teleported member's");
            else
                EncounterHelpers::ClearFocus(world, plan);
        }
    }

    void ScholomanceGandling::Propose(PartyBotAI const* bot, WorldModel const& world,
                                      TeamPlan const& /*plan*/, std::vector<Proposal>& out) const
    {
        if (m_guardians.empty() || bot->GetRole() == ROLE_HEALER)
            return;
        Player* me = bot->me;
        ObjectGuid const self = me->GetObjectGuid();
        int32 const room = TeleportedRoom(self);
        if (room == NO_ROOM)
            return;
        PartyMemberFacts const* myFacts = world.FindMember(self);
        if (!myFacts || !myFacts->CanAct())
            return;

        // Guardians of my room, or hitting me.
        auto mine = [&](Guardian const& g)
        {
            if (g.room == room)
                return true;
            EnemyFacts const* e = world.FindEnemy(g.guid);
            return e && e->victim == self;
        };

        Unit const* current = me->GetVictim();
        if (current)
            for (Guardian const& g : m_guardians)
                if (g.guid == current->GetObjectGuid() && mine(g))
                    return;     // already on one: the rotation goes on

        EnemyFacts const* best = nullptr;
        float bestDistance = 0.0f;
        for (Guardian const& g : m_guardians)
        {
            if (!mine(g))
                continue;
            EnemyFacts const* e = world.FindEnemy(g.guid);
            if (!e || !e->acting || !e->health)
                continue;
            float const d = PackResolver::Distance(myFacts->pos, e->pos);
            if (!best || d < bestDistance)
            {
                best = e;
                bestDistance = d;
            }
        }
        if (!best)
            return;     // room clear: wait for the door (see limits)

        Proposal p = EncounterHelpers::MakeReplace(SOURCE, ProposalTier::Urgent, BotActionType::Attack, best->guid);
        p.reason = "teleported by Gandling, fight the room's guardians";
        float const benefit = std::max(float(MIN_GUARD_POINTS), Value::HitLoss(world, *best, *myFacts, GUARD_HORIZON_SEC));
        p.SetValue(benefit, 0.0f);
        out.push_back(p);
    }
}
