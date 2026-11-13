#include "Commander/Behaviors/Movement/PatrolBehavior.h"
#include "Commander/Perception/Spatial/SafeRoute.h"
#include "Commander/Coordination/TeamPlan.h"
#include "PartyBotAI.h"
#include "Map.h"
#include <algorithm>
#include <cmath>
#include <functional>

namespace Commander
{
    namespace
    {
        Position3 Here(Player const* me)
        {
            Position3 here;
            here.x = me->GetPositionX();
            here.y = me->GetPositionY();
            here.z = me->GetPositionZ();
            return here;
        }

        uint32 WalkHoldMs(Player const* me, float distance)
        {
            float const speed = std::max(1.0f, me->GetSpeed(MOVE_RUN));
            return uint32(distance * PatrolBehavior::PATH_FACTOR / speed * 1000.0f) + PatrolBehavior::HOLD_EXTRA_MS;
        }

        bool IsMeleeRole(PartyBotAI const* bot)
        {
            return bot->GetRole() == ROLE_TANK || bot->GetRole() == ROLE_MELEE_DPS;
        }

        // Nearest enemy in sight that is not off limits (and not a caster of `lure`), within
        // reach of `from`: melee reach while hiding (movement is held), else TARGET_RANGE.
        Unit* PickTarget(PartyBotAI const* bot, WorldModel const& world, Position3 const& from,
                         PatrolLure const* lure, std::function<bool(ObjectGuid)> const& offLimits)
        {
            Player* me = bot->me;
            float const reach = lure && IsMeleeRole(bot) ? PatrolBehavior::HIDE_MELEE_REACH : PatrolBehavior::TARGET_RANGE;
            Unit* best = nullptr;
            float bestDistance = 0.0f;
            for (EnemyFacts const& e : world.enemies)
            {
                if (!e.acting || e.crowdControlled || !e.health || e.onAllyOnly || offLimits(e.guid))
                    continue;
                if (lure && std::find(lure->casters.begin(), lure->casters.end(), e.guid) != lure->casters.end())
                    continue;
                float const distance = SafeRoute::Distance2d(from, e.pos) - e.boundingRadius;
                if (distance > reach || (best && distance >= bestDistance))
                    continue;
                Unit* unit = me->GetMap()->GetUnit(e.guid);
                if (!unit || !unit->IsAlive() || !bot->IsValidHostileTarget(unit) || !me->IsWithinLOSInMap(unit))
                    continue;
                best = unit;
                bestDistance = distance;
            }
            return best;
        }

        Proposal MakeGuard(BotActionType type, float score, char const* reason)
        {
            Proposal p;
            p.tier = ProposalTier::Assigned;
            p.source = "patrol";
            p.score = score;
            p.reason = reason;
            p.action.type = type;
            return p;
        }

        // Guard (PatrolAssigner.h): hide from casters in the patrol's path, and stay off the
        // enemies standing in it.
        void ProposeGuard(PartyBotAI const* bot, WorldModel const& world, TeamPlan const& plan,
                          std::vector<Proposal>& out)
        {
            PatrolGuardPlan const& m = plan.patrolGuard;
            Player* me = bot->me;
            ObjectGuid const self = me->GetObjectGuid();
            // (the plan's constraint: the patrol's path unless it is this bot's intercept)
            auto const offLimits = [&plan, self](ObjectGuid g) { return !plan.MayEngage(self, g); };
            bool const healer = bot->GetRole() == ROLE_HEALER;
            Unit* victim = me->GetVictim();
            ObjectGuid const victimGuid = victim ? victim->GetObjectGuid() : ObjectGuid();

            if (PatrolLure const* lure = m.ActiveLureOf(self))
            {
                // Keep fighting while hiding: something in reach of the point, not the casters.
                bool const victimHidden = victim &&
                    (offLimits(victimGuid) ||
                     std::find(lure->casters.begin(), lure->casters.end(), victimGuid) != lure->casters.end());
                ObjectGuid face = victimHidden ? ObjectGuid() : victimGuid;
                // Only under the Backpedal hold: an Attack without it starts a chase
                // (AttackStart -> BeginChasing) away from the point. The first tick walks.
                if (!healer && (!victim || victimHidden) && bot->IsMovementHeld())
                {
                    if (Unit* other = PickTarget(bot, world, lure->point, lure, offLimits))
                    {
                        Proposal attack = MakeGuard(BotActionType::Attack, 6.0f, "fight on while hiding from a caster in the patrol's path");
                        attack.action.target = other->GetObjectGuid();
                        out.push_back(attack);
                        face = other->GetObjectGuid();
                    }
                }
                // Walk (backwards) to the point and stay there: the order is renewed every tick,
                // standing on the point too, so the chase does not take us back into its sight.
                Proposal back = MakeGuard(BotActionType::Backpedal, 5.0f, "out of sight of a caster in the patrol's path");
                back.action.target = face;
                back.action.x = lure->point.x;
                back.action.y = lure->point.y;
                back.action.z = lure->point.z;
                back.action.holdMs = PatrolBehavior::BACKPEDAL_ORDER_MS;
                out.push_back(back);
                if (!bot->IsMovementHeld())
                {
                    float const distance = SafeRoute::Distance2d(Here(me), lure->point);
                    Proposal forward = MakeGuard(BotActionType::MoveTo, 4.0f, "out of sight of a caster in the patrol's path");
                    forward.action.x = lure->point.x;
                    forward.action.y = lure->point.y;
                    forward.action.z = lure->point.z;
                    forward.action.holdMs = WalkHoldMs(me, distance);
                    out.push_back(forward);
                }
                return;
            }

            // A target standing in the patrol's path: TargetAssigner does not give it to anyone
            // (PatrolGuardPlan::avoid is one of its limits), so the bot gets another one.
        }
    }

    bool PatrolBehavior::AppliesTo(PartyBotAI const* bot) const
    {
        uint8 const role = bot->GetRole();
        return role == ROLE_TANK || role == ROLE_HEALER || role == ROLE_RANGE_DPS || role == ROLE_MELEE_DPS;
    }

    void PatrolBehavior::Propose(PartyBotAI const* bot, WorldModel const& world,
                                 TeamPlan const& plan, std::vector<Proposal>& out) const
    {
        if (!plan.patrolGuard.active || bot->me->HasUnitState(UNIT_STATE_ROOT))
            return;
        ProposeGuard(bot, world, plan, out);
    }
}
