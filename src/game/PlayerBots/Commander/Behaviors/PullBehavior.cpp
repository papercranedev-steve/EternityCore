#include "Commander/Behaviors/PullBehavior.h"
#include "Commander/Coordination/PullPlanner.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/PackResolver.h"
#include "PartyBotAI.h"
#include "DBCStores.h"
#include "Map.h"
#include "SpellMgr.h"
#include <algorithm>
#include <cmath>

namespace Commander
{
    namespace
    {
        Proposal Make(ProposalTier tier, float score, BotActionType type, ObjectGuid target = ObjectGuid())
        {
            Proposal p;
            p.tier = tier;
            p.score = score;
            p.source = "pull";
            p.action.type = type;
            p.action.target = target;
            return p;
        }

        Position3 PositionOf(WorldObject const* obj)
        {
            Position3 p;
            p.x = obj->GetPositionX();
            p.y = obj->GetPositionY();
            p.z = obj->GetPositionZ();
            return p;
        }

        Proposal MoveTo(Position3 const& to, float distance)
        {
            Proposal p = Make(ProposalTier::Assigned, 5.0f, BotActionType::MoveTo);
            p.action.x = to.x;
            p.action.y = to.y;
            p.action.z = to.z;
            p.action.holdMs = uint32(distance / PullBehavior::RUN_SPEED_YD_PER_SEC * 1000.0f) + 500;
            return p;
        }

        // Ranged pull (PullPlan::rangedPullSpell). Returns false when the tank should attack
        // normally instead (spell gone, already closer than the spell's minimum range).
        bool ProposeRangedPull(PartyBotAI const* bot, PullPlan const& pull, Unit* target, std::vector<Proposal>& out)
        {
            Player* me = bot->me;
            SpellEntry const* spell = sSpellMgr.GetSpellEntry(pull.rangedPullSpell);
            if (!spell)
                return false;
            SpellRangeEntry const* srange = sSpellRangeStore.LookupEntry(spell->rangeIndex);
            float const minRange = srange ? srange->minRange : 0.0f;
            float const distance = me->GetCombatDistance(target);
            if (minRange > 0.0f && distance < minRange)
                return false;

            // Shot on its way (cast time): let it finish.
            if (me->IsNonMeleeSpellCasted(false, false, true))
            {
                out.push_back(Make(ProposalTier::Assigned, 5.0f, BotActionType::Hold));
                return true;
            }

            bool const inRange = distance <= pull.rangedPullRange;
            if (inRange && me->IsWithinLOSInMap(target))
            {
                Proposal cast = Make(ProposalTier::Assigned, 5.0f, BotActionType::Cast, pull.target);
                cast.action.spell = spell;
                out.push_back(cast);
                // Not castable this tick (GCD, cooldown, form): stand still rather than letting the
                // original AI run in. PullAssigner falls back to a walk-in pull after a while.
                out.push_back(Make(ProposalTier::Assigned, 4.0f, BotActionType::Hold));
                return true;
            }

            // Still walking: do not restart the spline every tick.
            if (me->IsMoving() && bot->IsMovementHeld())
            {
                out.push_back(Make(ProposalTier::Assigned, 5.0f, BotActionType::Hold));
                return true;
            }

            Position3 const here = PositionOf(me);
            Position3 const there = PositionOf(target);
            if (inRange)
            {
                // In range but no line of sight: step towards the target, then look again.
                float const step = std::min(PullBehavior::LOS_STEP_YD, PackResolver::Distance(here, there));
                float const f = step / std::max(PackResolver::Distance(here, there), 0.1f);
                Position3 to;
                to.x = here.x + (there.x - here.x) * f;
                to.y = here.y + (there.y - here.y) * f;
                to.z = here.z + (there.z - here.z) * f;
                out.push_back(MoveTo(to, step));
                return true;
            }

            // Walk to a point inside the range on the line to the target: the tank stops there
            // instead of running into the pack (a hold left from walking would delay Return).
            float const total = PackResolver::Distance(here, there);
            float const keep = std::max(pull.rangedPullRange - PullPlanner::RANGED_STAND_MARGIN, minRange + 1.0f);
            float const walk = std::max(total - keep, 1.0f);
            float const f = walk / std::max(total, 0.1f);
            Position3 to;
            to.x = here.x + (there.x - here.x) * f;
            to.y = here.y + (there.y - here.y) * f;
            to.z = here.z + (there.z - here.z) * f;
            out.push_back(MoveTo(to, walk));
            return true;
        }
    }

    bool PullBehavior::AppliesTo(PartyBotAI const* bot) const
    {
        return bot->GetRole() != ROLE_INVALID;
    }

    void PullBehavior::Propose(PartyBotAI const* bot, WorldModel const& /*world*/,
                               TeamPlan const& plan, std::vector<Proposal>& out) const
    {
        PullPlan const& pull = plan.pull;
        if (pull.phase == PullPhase::None || pull.phase == PullPhase::WaitReady)
            return;

        Player* me = bot->me;

        if (me->GetObjectGuid() == pull.tank)
        {
            if (pull.phase == PullPhase::Approach)
            {
                Unit* target = me->GetMap()->GetUnit(pull.target);
                if (!target || !target->IsAlive())
                    return;
                if (pull.rangedPullSpell && ProposeRangedPull(bot, pull, target, out))
                    return;
                if (me->GetVictim() != target)
                    out.push_back(Make(ProposalTier::Assigned, 5.0f, BotActionType::Attack, pull.target));
            }
            else if (pull.phase == PullPhase::Return && !bot->IsMovementHeld())
            {
                float const distance = PackResolver::Distance(PositionOf(me), pull.spot);
                out.push_back(MoveTo(pull.spot, distance));
            }
            return;
        }

        // Everyone else waits for the tank. Higher-scored proposals (interrupts, threat) still win.
        // Only out of combat: a bot already in a fight (the player attacked first, a patrol joined)
        // must not stand idle for up to 20 seconds. Waiting for first threat is the threat module's job.
        if (me->IsInCombat())
            return;
        out.push_back(Make(ProposalTier::Assigned, 1.0f, BotActionType::Hold));
    }
}
