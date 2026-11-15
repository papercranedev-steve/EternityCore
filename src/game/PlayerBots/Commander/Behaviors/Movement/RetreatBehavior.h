/*
 * PartyBot Commander layer 3: executes TeamPlan::retreat (`.partybot ai retreat`).
 * Rules: combat scenario 11, T+20s .. T+40s (.kiro/docs/combat-scenarios/2026-11-01-11-open-world-elite.md).
 *
 * Every bot, every role, every tick while the retreat runs: one Override-tier Retreat action.
 *  - Override hides all other modules (taunts, interrupts, focus, pull): any hit or stun on an
 *    enemy extends its leash for the whole assisting group.
 *  - The action drops the target, stops auto shot and calls the pet back, then runs to the point.
 *  - Because the arbiter acts every tick, the original rotation never runs: no roots, stuns,
 *    taunts or heals.
 *  - Healers heal only at the point (2026-11-01): within ARRIVED_DISTANCE, not stepping aside,
 *    no enemy has the healer as victim. Target: most injured member below RETREAT_HEAL_PCT
 *    with no hostile unit in combat within RETREAT_HEAL_ENEMY_CLEARANCE of it (heal threat
 *    would put the healer on that creature's threat list). Instant heals only (instant direct
 *    heal, HoT, Power Word: Shield). Also an Override proposal, scored above the Retreat action;
 *    if the cast fails the arbiter runs the Retreat action in the same tick.
 *  - At the point, a bot an enemy is chasing keeps moving across the point: a chaser that
 *    reaches a target standing still for 5 s extends its leash (TargetedMovementGenerator ~390).
 *  - In a dungeon (RetreatPlan::toDoor, user 2026-11-15): back along the way in to the door,
 *    no heals on the way (a cast stops the run); RetreatPlanner::Step steps it out there.
 */

#ifndef MANGOS_COMMANDER_RETREAT_BEHAVIOR_H
#define MANGOS_COMMANDER_RETREAT_BEHAVIOR_H

#include "Commander/Behaviors/IBehavior.h"

class Unit;

namespace Commander
{
    class RetreatBehavior : public IBehavior
    {
    public:
        // Paths are longer than the straight line; the hold is renewed when it runs out anyway.
        static constexpr float PATH_FACTOR = 1.3f;
        static constexpr uint32 HOLD_EXTRA_MS = 1000;
        // At the point: a chaser this close makes the bot step to the other side of the point.
        static constexpr float CHASER_DISTANCE = 8.0f;
        // Stays inside RetreatAssigner::ARRIVED_DISTANCE and inside RetreatPlanner::SAFETY_MARGIN.
        static constexpr float SIDESTEP_DISTANCE = 7.0f;
        // Healing at the point.
        static constexpr float RETREAT_HEAL_PCT = 60.0f;
        static constexpr float RETREAT_HEAL_ENEMY_CLEARANCE = 30.0f;
        static constexpr float HEAL_SCORE = 2.0f;     // Retreat action has 1
        // Out through the door (RetreatPlan::toDoor): farther than this from the way, walk to it
        // first; on it, aim this many points on (WayIn::POINT_SPACING_YD each).
        static constexpr float DOOR_JOIN_YD = 4.0f;
        static constexpr size_t DOOR_LOOKAHEAD = 2;

        char const* GetId() const override { return "retreat"; }
        bool AppliesTo(PartyBotAI const* bot) const override;
        void Propose(PartyBotAI const* bot, WorldModel const& world,
                     TeamPlan const& plan, std::vector<Proposal>& out) const override;

    private:
        void ProposeHeal(PartyBotAI const* bot, WorldModel const& world, std::vector<Proposal>& out) const;
        static SpellEntry const* SelectInstantHeal(PartyBotAI const* bot, Unit const* pTarget);
    };
}

#endif
