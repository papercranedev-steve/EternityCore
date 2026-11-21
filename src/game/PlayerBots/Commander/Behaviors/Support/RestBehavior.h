/*
 * PartyBot Commander layer 3: eating and drinking ("rest"). Replaces the original PartyBot
 * eat and drink (TeamBotAI::DrinkAndEat, deleted 2026-11-07).
 * (user, 2026-11-07: pulls start once healers have the mana the fight needs; a caster still
 * sitting with a drink keeps drinking while the tank's pressure is low, to full, then heals or
 * fights; healers first take their places for the planned fight, then drink there.)
 *
 * PartyBotAI::UpdateAI runs the Commander hook out of combat whenever health or mana is missing
 * (TeamBotAI::NeedsRest) or the bot rests, and no longer stands a resting bot up itself (except
 * for a resurrection). A bot that is not in combat, has no victim and is not casting proposes
 * Rest (TeamBotAI::StartResting: eat what health is missing, drink what mana is), valued by
 * Perception/Values/RestValue.h:
 *  - no fight on or planned (after a fight, travelling): Routine tier, unvalued: anything else
 *    a module wants comes first, otherwise rest;
 *  - a pull planned or the party fighting: tier Assigned, benefit / cost of RestValue.
 * What it competes with each tick:
 *  - a healer's heal order (HealPlanBehavior): a routine heal of a lower score stands down,
 *    an emergency (Urgent) always comes first and the heal step stands the healer up;
 *  - walking to its place (stance modules): not while a walk is under way. While a fight is on
 *    or planned, a healer rests only in heal range (HealerStanceBehavior::HEAL_RANGE, in sight)
 *    of the main tank, or before the fight of the planned fight spot (PullPlan::
 *    PlannedFightSpot); a ranged damage dealer before the fight only in cast range
 *    (RangedStanceBehavior::CAST_RANGE, in sight) of the planned fight spot. Elsewhere it walks
 *    there first;
 *  - a damage dealer's target: TargetAssigner holds out a damage dealer whose rest pays (same
 *    RestValue), so it gets no target while resting.
 * The pull does not hold a resting bot (PullBehavior): it is waiting already.
 * The leader walking away (no fight on or planned): no rest until the bot has caught up
 * (TeamBotAI::HoldsOffRest, Behaviors/Movement/RestCatchUp.h; the bot stands up there).
 * A new food or drink only below RestValue::START_SHARE; the one on runs to full.
 * Health to mana first (ClassKit::HealthToMana, warlock Life Tap; user 2026-11-16): while one
 * tap shortens the rest by more than a global cooldown (RestValue::TapSavesSec: eating and
 * drinking run at once, the rest lasts the longer of the two), it proposes the tap in the
 * rest's place (same tier and value); then it sits down. Only with no fight on (no eating then).
 * Not while the retreat runs, while casting, or in combat (a hit stands the bot up and puts it
 * in combat; the game does not allow eating or drinking then).
 */

#ifndef MANGOS_COMMANDER_REST_BEHAVIOR_H
#define MANGOS_COMMANDER_REST_BEHAVIOR_H

#include "Commander/Behaviors/IBehavior.h"

namespace Commander
{
    class RestBehavior : public IBehavior
    {
    public:
        static constexpr float LOS_EYE_HEIGHT = 2.0f;

        char const* GetId() const override { return "rest"; }
        bool AppliesTo(TeamBotAI const* bot) const override;
        void Propose(TeamBotAI const* bot, WorldModel const& world,
                     TeamPlan const& plan, std::vector<Proposal>& out) const override;
    };
}

#endif
