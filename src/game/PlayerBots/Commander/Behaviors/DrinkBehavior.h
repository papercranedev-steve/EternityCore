/*
 * PartyBot Commander layer 3: drinking while the party fights or a pull runs ("drink").
 * (user, 2026-11-07: pulls start at 80% healer mana; a caster still sitting with a drink keeps
 * drinking while the tank's pressure is low, to full, then heals or fights.)
 *
 * Before: under a pull or the party's fight the original "eat and drink" stood down
 * (PartyBotAI::UpdateAI) and every tick stood the bot up, so the healer got up the moment the
 * tank went out.
 *
 * A bot that is not in combat, has a mana pool below full and no victim, proposes Drink (sit
 * down with the drink, or keep sitting), tier Assigned, valued by Perception/RestValue.h
 * (benefit: the mana to full for this fight; cost: a damage dealer's part of the fight while
 * sitting, 0 for healers). What it competes with each tick:
 *  - a healer's heal order (HealPlanBehavior): a routine heal of a lower score stands down,
 *    an emergency (Urgent) always comes first and the heal step stands the healer up;
 *  - walking to its place (stance modules): not while a walk is under way; a healer first
 *    walks into heal range (HealerStanceBehavior::HEAL_RANGE, in sight) of the main tank, or
 *    before the fight of the planned fight spot (PullPlan::PlannedFightSpot);
 *  - a damage dealer's target: TargetAssigner holds out a damage dealer whose drink pays
 *    (same RestValue), so it gets no target while drinking.
 * The pull does not hold a drinking bot (PullBehavior): it is waiting already.
 * Not while the retreat runs, while casting, or in combat (a hit stands the bot up and puts it
 * in combat; the game does not allow drinking then).
 */

#ifndef MANGOS_COMMANDER_DRINK_BEHAVIOR_H
#define MANGOS_COMMANDER_DRINK_BEHAVIOR_H

#include "Commander/Behaviors/IBehavior.h"

namespace Commander
{
    class DrinkBehavior : public IBehavior
    {
    public:
        static constexpr float LOS_EYE_HEIGHT = 2.0f;

        char const* GetId() const override { return "drink"; }
        bool AppliesTo(PartyBotAI const* bot) const override;
        void Propose(PartyBotAI const* bot, WorldModel const& world,
                     TeamPlan const& plan, std::vector<Proposal>& out) const override;
    };
}

#endif
