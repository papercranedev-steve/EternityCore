/*
 * PartyBot Commander layer 2 (brain): who gets the party's helping cooldowns (cooldown doc
 * .kiro/docs/2026-11-14-cooldowns.md 4): Innervate, Power Infusion, Mana Tide Totem
 * (ClassKit::AllyBoosts). Duties module, the RescueAssigner pattern: while the party fights,
 * one need per party member that some bot's boost is worth more than it costs on
 * (Perception/Values/BoostValue.h, net > 0); candidates = those bots with that spell, rank = net
 * (+ STICKY_POINTS for last refresh's choice). One primary duty per bot (DutyAllocator): a
 * druid's Innervate goes to the member it is worth most on. BoostBehavior casts it.
 */

#ifndef MANGOS_COMMANDER_BOOST_ASSIGNER_H
#define MANGOS_COMMANDER_BOOST_ASSIGNER_H

#include "Commander/Coordination/IAssigner.h"

namespace Commander
{
    class BoostAssigner : public IAssigner
    {
    public:
        static constexpr float STICKY_POINTS = 0.5f;

        // Same id as BoostBehavior: `.partybot ai module boost off` switches both.
        char const* GetId() const override { return "boost"; }
        AssignStage GetStage() const override { return AssignStage::Duties; }
        void Offer(WorldModel const& world, TeamPlan const& plan, std::vector<DutyNeed>& out) const override;
        void Apply(WorldModel const& world, std::vector<DutyResult> const& results, TeamPlan& plan) const override;
        void Clear(TeamPlan& plan) const override;
    };
}

#endif
