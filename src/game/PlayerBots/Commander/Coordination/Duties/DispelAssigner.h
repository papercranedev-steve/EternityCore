/*
 * PartyBot Commander layer 2 (brain): who removes which debuff (2026-11-03, takeover of dispels,
 * architecture refinement 5.2). Duties module: one need per party member and debuff worth more
 * than nothing (Perception/Values/DispelValue.h); candidates are bots that can act with a ready
 * dispel of the right type, the member within its range (line of sight is checked live by
 * DispelBehavior). A member mind controlled by a creature only by a dispel that can target a
 * hostile unit (Dispel Magic). Cost of a candidate = one global cooldown of its output
 * (Value::LostAction); offered only if the debuff is worth more. Rank = -cost, the previous
 * dispeller of the same debuff gets STICKY_SEC of output off.
 * Control effects are urgent (need "now" and DispelDuty::control); the others are answered by
 * points after them. Each bot takes one primary duty (DutyAllocator), so two healers do not
 * remove the same debuff, and a bot already interrupting is not given a dispel too.
 * In combat only: out of combat the rotation dispels as before.
 */

#ifndef MANGOS_COMMANDER_DISPEL_ASSIGNER_H
#define MANGOS_COMMANDER_DISPEL_ASSIGNER_H

#include "Commander/Coordination/IAssigner.h"

namespace Commander
{
    class DispelAssigner : public IAssigner
    {
    public:
        static constexpr float STICKY_SEC = 0.5f;

        // Same id as DispelBehavior: `.partybot ai module dispel off` switches both.
        char const* GetId() const override { return "dispel"; }
        AssignStage GetStage() const override { return AssignStage::Duties; }
        void Offer(WorldModel const& world, TeamPlan const& plan, std::vector<DutyNeed>& out) const override;
        void Apply(WorldModel const& world, std::vector<DutyResult> const& results, TeamPlan& plan) const override;
        void Clear(TeamPlan& plan) const override;
    };
}

#endif
