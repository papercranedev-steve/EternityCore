#include "Commander/Behaviors/DrinkBehavior.h"
#include "Commander/Behaviors/HealerStanceBehavior.h"
#include "Commander/Behaviors/SafeRoute.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/RestValue.h"
#include "Commander/Perception/Tanks.h"
#include "PartyBotAI.h"
#include "Map.h"

namespace Commander
{
    namespace
    {
        // A healer drinks where it can heal from: in heal range and sight of the main tank, or
        // before the fight of the planned fight spot. No anchor: anywhere.
        bool HealerPlaceOk(PartyBotAI const* bot, WorldModel const& world, TeamPlan const& plan,
                           PartyMemberFacts const& self)
        {
            Position3 anchor;
            bool const planned = !world.HasPartyEnemy() && plan.pull.PlannedFightSpot(anchor);
            if (!planned)
            {
                PartyMemberFacts const* tank = Tanks::Main(world);
                if (!tank || tank->guid == self.guid)
                    return true;
                anchor = tank->pos;
            }
            if (SafeRoute::Distance2d(self.pos, anchor) > HealerStanceBehavior::HEAL_RANGE)
                return false;
            float const eye = DrinkBehavior::LOS_EYE_HEIGHT;
            return bot->me->GetMap()->isInLineOfSight(self.pos.x, self.pos.y, self.pos.z + eye,
                                                      anchor.x, anchor.y, anchor.z + eye);
        }
    }

    bool DrinkBehavior::AppliesTo(PartyBotAI const* bot) const
    {
        return bot->GetRole() != ROLE_TANK;
    }

    void DrinkBehavior::Propose(PartyBotAI const* bot, WorldModel const& world,
                                TeamPlan const& plan, std::vector<Proposal>& out) const
    {
        if (plan.retreat.active)
            return;
        Player* me = bot->me;
        if (me->IsInCombat() || me->GetVictim() || me->IsNonMeleeSpellCasted(false, false, true))
            return;
        // Walking to its place (stance modules, the pull's hiding point): arrive first.
        if (bot->IsMovementHeld() && me->IsMoving())
            return;
        PartyMemberFacts const* self = world.FindMember(me->GetObjectGuid());
        if (!self || !self->CanAct())
            return;

        RestValue::Worth const w = RestValue::OfDrinking(world, *self, plan.pull.PlannedFight());
        if (!w.Pays())
            return;
        if (bot->GetRole() == ROLE_HEALER && !HealerPlaceOk(bot, world, plan, *self))
            return;

        Proposal p;
        p.tier = ProposalTier::Assigned;
        p.source = "drink";
        p.reason = self->drinking ? "keep drinking, mana for this fight" : "drink, mana for this fight";
        p.action.type = BotActionType::Drink;
        p.SetValue(w.benefit, w.cost);
        out.push_back(p);
    }
}
