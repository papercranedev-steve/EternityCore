/*
 * PartyBot Commander layer 2 (brain): team rescue, first part (todo item 20, user 2026-11-07;
 * .kiro/docs/2026-11-07/2026-11-07-team-rescue.md). An enemy hitting a member who is not a tank
 * (RescueValue::LooseVictim) gets ONE way off its victim for the whole party, instead of the
 * tank taunting, the tank walking over and the priest fading at once, each on its own.
 * Duties module.
 *
 * Offer, while the party fights: one need per loose enemy (now = true, kind Rescue,
 * exclusive DutyExclusive::EnemyHandled: a single target control or an intercept of the same
 * enemy is an alternative, whichever is worth more is answered). Not for an enemy the party
 * stays off (PatrolGuardPlan::Avoids) or the player said to leave alone. Candidates
 * (Perception/Values/RescueValue.h), each offered when its net is positive, rank = net (+ STICKY_POINTS
 * for the bot and spell given it last refresh):
 *   every tank bot that can act within TANK_REACH yd: each ready taunt (spellId), and winning it
 *     back with threat (spellId 0). A walk that would pull an idle pack or finds no way
 *     (RouteRisk::Blocked) is not offered: only a taunt already in range;
 *   the victim itself when it is a bot with a ready threat drop (Fade, its spellId).
 * points = the best net; detail DETAIL_URGENT when the victim dies within the horizon (or,
 * fading, within RescueValue::FADE_URGENT_SEC).
 * Apply: plan.rescues (TeamPlan::RescueDuty, the way from the chosen spell).
 *
 * Who executes: RescueBehavior casts the taunt / Fade (and takes both over from the rotations);
 * TargetAssigner gives the tank the enemy when it wins it back with threat, or must walk into
 * taunt range first (TankTargets.cpp).
 *
 * Not yet (doc section 5): a shield on the victim, the victim running to the tank
 * (EscapeBehavior still decides that by itself, it only makes the tank's walk shorter), and
 * "someone is about to die" (Shield Wall, Last Stand, Blessing of Protection, Lay on Hands).
 */

#ifndef MANGOS_COMMANDER_RESCUE_ASSIGNER_H
#define MANGOS_COMMANDER_RESCUE_ASSIGNER_H

#include "Commander/Coordination/IAssigner.h"

namespace Commander
{
    class RescueAssigner : public IAssigner
    {
    public:
        // Loose enemies further than this from a tank, or out of its sight, are another fight
        // (a teleported member's room): same as TankTargets::TANK_REACH.
        static constexpr float TANK_REACH = 40.0f;
        static constexpr float MELEE_REACH = 5.0f;
        static constexpr float STICKY_POINTS = 0.5f;
        static constexpr uint8 DETAIL_URGENT = 1;

        // Same id as RescueBehavior: `.partybot ai module rescue off` switches both.
        char const* GetId() const override { return "rescue"; }
        AssignStage GetStage() const override { return AssignStage::Duties; }
        void Offer(WorldModel const& world, TeamPlan const& plan, std::vector<DutyNeed>& out) const override;
        void Apply(WorldModel const& world, std::vector<DutyResult> const& results, TeamPlan& plan) const override;
        void Clear(TeamPlan& plan) const override;
    };
}

#endif
