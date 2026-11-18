/*
 * PartyBot Commander layer 2: `.partybot ai retreat` (first version, player command only).
 * Rules come from combat scenario 11, section 1.5 and T+18s .. T+40s
 * (.kiro/docs/combat-scenarios/2026-11-01-11-open-world-elite.md).
 *
 * Server rule (Creature::IsOutOfThreatArea, Creature.cpp ~2796): outside dungeons a creature
 * gives up when both it and its victim are farther than
 *     D = max(GetAttackDistance(victim) * 1.5, ThreatRadius)       ThreatRadius default 50
 * from where it entered combat (Creature::GetCombatStartPosition), and its leash was not
 * extended for 12 seconds (hit, crowd controlled, or caught a target standing still for 5 s).
 *
 * RetreatPlanner::Start - world thread (GM command, maps idle), or the map thread (Step, the
 *                         leader on this map): picks a rally point out of every enemy's threat
 *                         area, interrupts bot casts, stops pull / clear. In a dungeon:
 *                         StartDoor (out through the door the party came in by).
 * RetreatPlanner::Stop  - `.partybot ai retreat stop`.
 * RetreatPlanner::Step  - map thread: the retreat RunAwayAssigner asked for (the player runs).
 * RetreatAssigner       - pure; ends the retreat (done / timeout) and, while no retreat runs,
 *                         sends one "consider retreating" hint per fight.
 */

#ifndef MANGOS_COMMANDER_RETREAT_PLANNER_H
#define MANGOS_COMMANDER_RETREAT_PLANNER_H

#include "Commander/Coordination/IAssigner.h"
#include "Commander/Coordination/TeamPlan.h"
#include <string>
#include <vector>

class Player;
class TeamBotAI;
class Map;

namespace Commander
{
    class PartyCommander;

    namespace RetreatPlanner
    {
        // World.cpp: setConfig(CONFIG_FLOAT_THREAT_RADIUS, "ThreatRadius", 50.0f). Not read from
        // sWorld on purpose: a constant keeps the planner's numbers fixed for the docs and tests.
        float const THREAT_RADIUS = 50.0f;
        // IsOutOfThreatArea uses attack distance * 1.5 when that is larger than ThreatRadius.
        float const ATTACK_DISTANCE_FACTOR = 1.5f;
        // Extra distance beyond D: the point is a gathering spot, members stand up to
        // ARRIVED_DISTANCE away from it, and creatures keep chasing a bit past the edge.
        float const SAFETY_MARGIN = 10.0f;
        // No idle hostile creature this close to the point (or within its own aggro range).
        float const IDLE_CREATURE_CLEARANCE = 25.0f;

        // Candidate search: from the leader, along "enemies' combat start -> leader" (the way
        // the party came, roughly; there is no recorded path yet), turned by these angles.
        float const SEARCH_STEP = 5.0f;
        float const SEARCH_MAX_DISTANCE = 100.0f;
        // Ground height must be found within this much of the leader's height (no cliffs, no caves above).
        float const MAX_HEIGHT_DIFFERENCE = 30.0f;
        // Path may be at most this much longer than the straight line (long detours run through
        // places nobody looked at).
        float const MAX_PATH_DETOUR = 2.0f;
        // Pathfinding is the expensive check (world thread): only the best this many candidates
        // that passed the cheap checks (ground, threat areas, idle hostiles) are pathfound.
        // 16, not 6: sorted by distance, 6 were the 5 turns of one ring plus one point, and in
        // a bending corridor all of them can be walls (Start runs once per command, not per tick).
        uint32 const MAX_PATH_CHECKS = 16;

        // Leader = the player who gave the command. In a dungeon: StartDoor. False with the
        // reason in `report`.
        bool Start(Player* leader, PartyCommander& commander, std::vector<std::string>& report);
        // Ends a running retreat and lets the bots move on their own again.
        void Stop(Player* leader, PartyCommander& commander, std::string const& why);

        // In a dungeon (user 2026-11-15): out through the door along the leader's way in
        // (Perception/Spatial/WayIn.h). Only when every living member joins that way, it has no
        // drop to climb, the door is at most MAX_DOOR_YD back for each, and none would die on the
        // way: a member that enemies hit for `dps` (their melee and ranged damage, acting, not
        // controlled) lives health / dps, the run takes walk / RUN_YD_PER_SEC x RUN_MARGIN.
        // Farther or slower: the door is no way out (the wipe protection comes later). `map`:
        // the leader may have left it already (out through the door himself).
        bool StartDoor(Map* map, PartyCommander& commander, std::vector<std::string>& report);
        float const MAX_DOOR_YD = 120.0f;
        float const RUN_YD_PER_SEC = 7.0f;
        float const RUN_MARGIN = 1.5f;
        // A bot this close to the door end steps out (TeleportTo the dungeon's outside point:
        // bots send no area trigger packet, the walk through the portal is done for them).
        float const DOOR_REACH_YD = 4.0f;

        // Map step after the assigners (PartyCommander::Update): starts a retreat RunAwayAssigner
        // asked for (and tells the leader, or why it cannot), steps door runners out.
        void Step(TeamBotAI* bot, PartyCommander& commander);
    }

    class RetreatAssigner : public IAssigner
    {
    public:
        // Scenario 11: about 20 s running (T+20s .. T+40s) plus the 12 s leash timer, with room.
        static constexpr uint32 TIMEOUT_MS = 45 * 1000;
        static constexpr float ARRIVED_DISTANCE = 10.0f;

        // Hint "consider retreating", sent at most once per fight (bots never retreat on their own).
        //  a) incoming damage per second above what the healers can heal, for DANGER_SUSTAIN_MS.
        //     Healing capacity: per healer bot that can act and has at least HEALER_MIN_MANA_PCT
        //     mana, its fastest direct heal chain cast (HealCapacity::MaxHps: real spells,
        //     +healing gear, crit) less ManaBudget::OVERHEAL; HEALER_HPS_PER_LEVEL * level
        //     only for a healer bot without direct heals.
        //     Parties without a healer bot only use (b), otherwise every fight would trigger it.
        //  b) a tank bot below TANK_LOW_HEALTH_PCT and no healer bot can act (dead, controlled, oom).
        static constexpr uint32 DANGER_SUSTAIN_MS = 5000;
        static constexpr float HEALER_HPS_PER_LEVEL = 4.0f;
        static constexpr float HEALER_MIN_MANA_PCT = 10.0f;
        static constexpr float TANK_LOW_HEALTH_PCT = 25.0f;

        char const* GetId() const override { return "retreat"; }
        void Assign(WorldModel const& world, TeamPlan& plan) const override;
        void Clear(TeamPlan& plan) const override;

        // Pure: true with the reason when the party is in danger. `sustained` = rule (a), which
        // must hold for DANGER_SUSTAIN_MS before the hint is sent.
        static bool InDanger(WorldModel const& world, std::string& reason, bool& sustained);

    private:
        static void Advise(WorldModel const& world, TeamPlan& plan);
    };
}

#endif
