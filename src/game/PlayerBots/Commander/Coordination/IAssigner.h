/*
 * PartyBot Commander layer 2: team assignment modules.
 * Assign runs once per perception refresh of a party, after the world model is rebuilt.
 * Assigners are shared by all parties on all map threads: no member state, read the
 * previous assignment from `plan` instead (it still holds the last result).
 * Assigners are pure functions of WorldModel + TeamPlan, so they can be replayed offline.
 * Jev hooks (BuildQuestion / ApplyAnswer, code-structure doc 3.2) come later.
 *
 * Two steps (2026-11-03 architecture refinement 3.2), run by PartyCommander::RunAssigners:
 *  1. Situation - what kind of moment this is (pull, retreat, moving off a patrol, roles).
 *     Assign writes the plan directly, in registration order; later stages read the result.
 *  2. Tasks     - who does what. Duties modules only Offer needs; DutyAllocator answers all
 *     of them together (one primary duty per bot) and each module writes its part (Apply).
 *     Tasks modules not moved to offers yet (none left since 2026-11-03) would still Assign
 *     directly, before the duties. New task modules are written as Duties.
 */

#ifndef MANGOS_COMMANDER_IASSIGNER_H
#define MANGOS_COMMANDER_IASSIGNER_H

#include "Common.h"
#include "Commander/Coordination/DutyAllocator.h"
#include <vector>

namespace Commander
{
    struct WorldModel;
    struct TeamPlan;

    enum class AssignStage : uint8
    {
        Situation,
        Tasks,      // still assigns directly (to be moved to Duties)
        Duties,     // Offer / Apply through DutyAllocator
    };

    class IAssigner
    {
    public:
        virtual ~IAssigner() = default;
        virtual char const* GetId() const = 0;
        virtual AssignStage GetStage() const { return AssignStage::Situation; }

        // Situation and Tasks stages.
        virtual void Assign(WorldModel const& /*world*/, TeamPlan& /*plan*/) const {}

        // Duties stage: the needs this module wants answered (plan = situation of this refresh
        // plus the last results of this module, e.g. for sticky choices) ...
        virtual void Offer(WorldModel const& /*world*/, TeamPlan const& /*plan*/, std::vector<DutyNeed>& /*out*/) const {}
        // ... the needs it expects later, which spending a cooldown now may leave unanswered
        // (DutyReserve: team cooldown budget) ...
        virtual void OfferReserves(WorldModel const& /*world*/, TeamPlan const& /*plan*/, std::vector<DutyReserve>& /*out*/) const {}
        // ... and its part of the plan from the allocation (all results; take your own kind).
        virtual void Apply(WorldModel const& /*world*/, std::vector<DutyResult> const& /*results*/, TeamPlan& /*plan*/) const {}

        // Called instead of Assign / Offer when the module is disabled.
        virtual void Clear(TeamPlan& plan) const = 0;
    };
}

#endif
