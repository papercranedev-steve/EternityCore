#include "Commander/Behaviors/CrowdControlBehavior.h"
#include "Commander/Classes/ClassKit.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Coordination/PullControl.h"
#include "Commander/Perception/PackResolver.h"
#include "Commander/Perception/ValueModel.h"
#include "Commander/Perception/WorldModel.h"
#include "PartyBotAI.h"
#include "Map.h"
#include "SpellEntry.h"
#include "SpellMgr.h"

namespace Commander
{
    bool CrowdControlBehavior::AppliesTo(PartyBotAI const* bot) const
    {
        for (SpellEntry const* s : bot->Kit().Controls())
            if (s)
                return true;
        return false;
    }

    namespace
    {
        // This bot walks to Sap the opening target before the tank pulls (PullControl.h).
        bool IsSapper(PartyBotAI const* bot, TeamPlan const& plan)
        {
            PullControlPlan const& opening = plan.pull.control;
            return opening.Active() && opening.beforeOpener && !opening.landed &&
                   opening.bot == bot->me->GetObjectGuid() && plan.pull.phase == PullPhase::Approach &&
                   !bot->me->IsInCombat();
        }

        Proposal Opening(char const* reason, BotActionType type)
        {
            Proposal p;
            p.tier = ProposalTier::Assigned;
            p.source = "cc";
            p.reason = reason;
            p.score = 5.0f;
            p.action.type = type;
            return p;
        }
    }

    bool CrowdControlBehavior::TakesOver(Takeover what, PartyBotAI const* bot, WorldModel const& /*world*/,
                                         TeamPlan const& plan) const
    {
        if (bot->IsInDuel())
            return false;
        if (what == Takeover::Stealth)
            return IsSapper(bot, plan);
        return what == Takeover::CrowdControl && bot->me->IsInCombat();
    }

    void CrowdControlBehavior::ProposeSap(PartyBotAI const* bot, TeamPlan const& plan,
                                          PartyMemberFacts const& self, std::vector<Proposal>& out) const
    {
        Player* me = bot->me;
        PullControlPlan const& opening = plan.pull.control;
        // Cast: stay put until the fight starts (out of stealth now; the plan sees the Sap at
        // its next pack refresh, the live aura counts at once).
        Unit* target = me->GetMap()->GetUnit(opening.target);
        if (opening.landed || (target && target->HasAura(opening.spellId)))
        {
            out.push_back(Opening("opening sap: wait for the pull", BotActionType::Hold));
            return;
        }
        if (!me->HasAuraType(SPELL_AURA_MOD_STEALTH))
        {
            SpellEntry const* stealth = bot->Kit().Stealth();
            if (!stealth || !bot->CanTryToCastSpell(me, stealth))
                return;     // PullControl drops it when the walk does not start in time
            Proposal p = Opening("opening sap: stealth", BotActionType::Cast);
            p.action.spell = stealth;
            p.action.target = me->GetObjectGuid();
            out.push_back(p);
            return;
        }
        float const left = PackResolver::Distance(self.pos, opening.spot);
        if (left > PullControl::ARRIVED_YD && !(bot->IsMovementHeld() && me->IsMoving()))
        {
            Proposal p = Opening("opening sap: sneak to the spot", BotActionType::MoveTo);
            p.action.x = opening.spot.x;
            p.action.y = opening.spot.y;
            p.action.z = opening.spot.z;
            p.action.holdMs = uint32(left / PullControl::STEALTH_WALK_YD_PER_SEC * 1000.0f) + 1000;
            out.push_back(p);
            return;
        }
        SpellEntry const* sap = sSpellMgr.GetSpellEntry(opening.spellId);
        if (left <= PullControl::ARRIVED_YD && sap && target && target->IsAlive() && !target->IsInCombat() &&
            me->IsWithinLOSInMap(target) && bot->CanTryToCastSpell(target, sap))
        {
            Proposal p = Opening("opening sap: cast", BotActionType::Cast);
            p.action.spell = sap;
            p.action.target = opening.target;
            out.push_back(p);
            return;
        }
        out.push_back(Opening("opening sap: wait at the spot", BotActionType::Hold));
    }

    void CrowdControlBehavior::Propose(PartyBotAI const* bot, WorldModel const& world,
                                       TeamPlan const& plan, std::vector<Proposal>& out) const
    {
        Player* me = bot->me;
        PartyMemberFacts const* self = world.FindMember(me->GetObjectGuid());
        if (!self || !self->CanAct())
            return;

        // Opening controller of a pull (PullControl.h), before the fight: walk to the spot and
        // stay there (the order is renewed every tick, so following the leader does not take it away).
        PullControlPlan const& opening = plan.pull.control;
        if (opening.Active() && opening.bot == self->guid && plan.pull.phase == PullPhase::Approach &&
            !world.HasPartyEnemy())
        {
            if (opening.beforeOpener)
            {
                ProposeSap(bot, plan, *self, out);
                return;
            }
            Proposal p;
            p.tier = ProposalTier::Assigned;
            p.source = "cc";
            p.reason = "opening control: to the spot";
            p.score = 5.0f;
            float const left = PackResolver::Distance(self->pos, opening.spot);
            if (left <= PullControl::ARRIVED_YD || (bot->IsMovementHeld() && me->IsMoving()))
                p.action.type = BotActionType::Hold;
            else
            {
                p.action.type = BotActionType::MoveTo;
                p.action.x = opening.spot.x;
                p.action.y = opening.spot.y;
                p.action.z = opening.spot.z;
                p.action.holdMs = uint32(left / OPENING_WALK_YD_PER_SEC * 1000.0f) + 1000;
            }
            out.push_back(p);
            return;
        }

        // Only controlled enemies left (ControlledEnd.h): TargetAssigner gives the breaker that
        // target, TargetBehavior attacks it.

        // Cast once the party fights (the controller of an opening may not be in combat yet).
        ControlDuty const* duty = plan.FindControlFor(self->guid);
        if (!duty || !world.HasPartyEnemy())
            return;
        SpellEntry const* spell = sSpellMgr.GetSpellEntry(duty->spellId);
        if (!spell)
            return;

        // Psychic Scream and Howl of Terror are cast on the bot itself; Intimidating Shout and
        // every single target spell on the enemy (the shout's target cowers, the rest runs).
        Unit* target = nullptr;
        if (spell->rangeIndex != SPELL_RANGE_IDX_SELF_ONLY)
        {
            target = me->GetMap()->GetUnit(duty->enemy);
            if (!target || !target->IsAlive() || (!duty->area && target->HasAura(spell->Id)) ||
                !me->IsWithinLOSInMap(target) || !bot->CanTryToCastSpell(target, spell))
                return;
        }
        else if (!bot->CanTryToCastSpell(me, spell))
            return;

        float const cost = Value::LostAction(world, *self, Value::GCD_SEC);
        Proposal p;
        p.tier = ProposalTier::Assigned;
        p.source = "cc";
        p.reason = duty->area ? "assigned: area control" : "assigned: control";
        p.action.type = BotActionType::Cast;
        p.action.spell = spell;
        if (target)
            p.action.target = target->GetObjectGuid();
        p.action.dropTarget = duty->breaksOnDamage;
        p.SetValue(duty->points + cost, cost);
        out.push_back(p);
    }

    bool CrowdControlBehavior::WantsTickNow(PartyBotAI const* bot, WorldModel const& world, TeamPlan const& plan) const
    {
        return world.HasPartyEnemy() && plan.FindControlFor(bot->me->GetObjectGuid()) != nullptr;
    }
}
