/*
 * PartyBot Commander layer 2: whom each dwarf priest bot keeps Fear Ward on ("fear ward"
 * assigner, boss doc G8 .kiro/docs/2026-11-28-dungeon-boss-encounters.md). Fear Ward (6346):
 * the next fear on the target does nothing; one charge, 10 min, 30 s cooldown.
 *
 * Points of a member: the fears sure to reach it within the timeline's horizon
 * (IncomingTimeline::Lockout with MECHANIC_FEAR; a member immune to fear already has none),
 * each Value::LostAction over its length. Random target fears are not sure: none.
 * Each priest bot (guid order) takes the member with the most points no priest before it took,
 * the first fear far enough off to ward it first (WARD_LEAD_SEC). None with points: the main
 * tank (Tanks::Main, else the hit taker) with 0 points, kept up between fights by the
 * rotation (an area fear around a boss always reaches him, a fear on the victim too).
 *
 * Not a Duties module, like the totems: a ward is a standing buff, the priest still heals.
 * Runs every refresh (pure function of the world model). FearWardBehavior casts it in a fight
 * when the points pay for the global cooldown; PriestRotation keeps it on the target out of one.
 */

#ifndef MANGOS_COMMANDER_FEAR_WARD_ASSIGNER_H
#define MANGOS_COMMANDER_FEAR_WARD_ASSIGNER_H

#include "Commander/Coordination/IAssigner.h"

namespace Commander
{
    class FearWardAssigner : public IAssigner
    {
    public:
        // A fear sooner than this cannot be warded first (the cast and its travel to the target).
        static constexpr float WARD_LEAD_SEC = 0.3f;

        char const* GetId() const override { return "fear ward"; }
        AssignStage GetStage() const override { return AssignStage::Tasks; }
        void Assign(WorldModel const& world, TeamPlan& plan) const override;
        void Clear(TeamPlan& plan) const override;
    };
}

#endif
