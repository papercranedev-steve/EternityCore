/*
 * PartyBot Commander layer 2 (brain): who removes which debuff (2026-11-03, takeover of dispels,
 * architecture refinement 5.2). Duties module: one need per party member and debuff worth more
 * than nothing (Perception/Values/DispelValue.h); candidates are bots that can act with a ready
 * dispel of the right type, or a removal by mechanic (Blessing of Freedom on a root or slow
 * with the mechanic, any dispel type; user 2026-11-26: a mutual aid, players too), the member
 * within its range (line of sight is checked live by DispelBehavior). A bot offers its best
 * removal: one that leaves an immunity is valued with it (DispelValue immuneSec), so on a root
 * the caster keeps putting back, Blessing of Freedom beats Cleanse. A root or slow is worth
 * something only while the member has to move (Perception/Spatial/MoveNeed.h) or the plan
 * retreats. Extra cost of a removal: its cooldown (Value::CooldownPoints), and for a blessing
 * the caster's other blessing it replaces on the member (LOST_BLESSING_SHARE). A member mind controlled by a creature only by a dispel that can target a
 * hostile unit (Dispel Magic). Cost of a candidate = one global cooldown of its output
 * (Value::LostAction); offered only if the debuff is worth more. Rank = -cost, the previous
 * dispeller of the same debuff gets STICKY_SEC of output off.
 * Control effects and mana leeches a healer cannot spare are urgent (need "now",
 * DispelDuty::control / manaCritical); the others are answered by points after them. Each bot takes one primary duty (DutyAllocator), so two healers do not
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
        // A removal that is a blessing (Blessing of Freedom) replaces the caster's other
        // blessing on the member: that one's worth, a guess, as this share of the member's
        // output for the rest of the fight (at most MAX_SEC of it).
        static constexpr float LOST_BLESSING_SHARE = 0.05f;
        static constexpr float LOST_BLESSING_MAX_SEC = 60.0f;
        // DutyNeed::detail bits, passed through to DispelDuty.
        static constexpr uint8 DETAIL_CONTROL = 1;
        static constexpr uint8 DETAIL_MANA_CRITICAL = 2;

        // Same id as DispelBehavior: `.partybot ai module dispel off` switches both.
        char const* GetId() const override { return "dispel"; }
        AssignStage GetStage() const override { return AssignStage::Duties; }
        void Offer(WorldModel const& world, TeamPlan const& plan, std::vector<DutyNeed>& out) const override;
        void Apply(WorldModel const& world, std::vector<DutyResult> const& results, TeamPlan& plan) const override;
        void Clear(TeamPlan& plan) const override;
    };
}

#endif
