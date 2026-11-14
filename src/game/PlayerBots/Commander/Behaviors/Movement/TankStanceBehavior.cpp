#include "Commander/Behaviors/Movement/TankStanceBehavior.h"
#include "Commander/Coordination/Situation/FightSpot/FightSpotAssigner.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/Profiles/Tags.h"
#include "Commander/Perception/Spatial/SafeRoute.h"
#include "Commander/Perception/Values/ValueModel.h"
#include "PartyBotAI.h"
#include "Map.h"
#include <algorithm>
#include <cmath>

namespace Commander
{
    namespace
    {
        using TS = TankStanceBehavior;

        Position3 Here(Player const* me)
        {
            Position3 here;
            here.x = me->GetPositionX();
            here.y = me->GetPositionY();
            here.z = me->GetPositionZ();
            return here;
        }

        Proposal Make(ProposalTier tier, BotActionType type, char const* reason)
        {
            Proposal p;
            p.tier = tier;
            p.source = "tankstance";
            p.reason = reason;
            p.action.type = type;
            return p;
        }

        // `s` with its ground height, within MAX_STEP_Z of `nearZ`, outside every idle
        // creature's reach and reachable on a safe path.
        bool SpotOk(Player* me, SafeRoute::IdleCreatures const& idle, float nearZ, Position3& s)
        {
            float const ground = me->GetMap()->GetHeight(s.x, s.y, nearZ + 3.0f);
            if (ground <= INVALID_HEIGHT || std::fabs(ground - nearZ) > TS::MAX_STEP_Z)
                return false;
            s.z = ground;
            return !idle.Near(s) && SafeRoute::PathIsSafe(me, idle, s, TS::MAX_PATH_DETOUR);
        }

        uint32 WalkHoldMs(Player const* me, float distance)
        {
            float const speed = std::max(1.0f, me->GetSpeed(MOVE_RUN));
            return uint32(distance * TS::PATH_FACTOR / speed * 1000.0f) + TS::HOLD_EXTRA_MS;
        }

        // The fight spot moves this tank (header, first bullet).
        void ProposeMove(PartyBotAI const* bot, FightSpotPlan const& m, std::vector<Proposal>& out)
        {
            Player* me = bot->me;
            // Walking there forwards (no backwards path): keep walking (an instant ability is fine,
            // a cast would stop the walk). Walking backwards: the order below is renewed instead,
            // and the rotation keeps fighting.
            if (bot->IsMovementHeld() && me->IsMoving() && !bot->IsBackpedaling())
            {
                out.push_back(Make(ProposalTier::Assigned, BotActionType::Hold, "moving the fight, keep walking"));
                return;
            }

            Position3 const here = Here(me);
            if (m.chosen == -1)
            {
                SafeRoute::IdleCreatures const idle(me);
                m.chosen = -2;
                for (size_t i = 0; i < m.spots.size(); ++i)
                {
                    Position3 s = m.spots[i];
                    if (!SpotOk(me, idle, here.z, s))
                        continue;
                    m.chosen = int32(i);
                    m.chosenSpot = s;
                    break;
                }
            }
            if (m.chosen < 0)
                return;

            Position3 const& to = m.chosenSpot;
            float const distance = SafeRoute::Distance2d(here, to);
            if (distance <= FightSpotAssigner::ARRIVED_DISTANCE)
                return;
            Proposal p = Make(ProposalTier::Assigned, BotActionType::MoveTo, "move the fight (fight spot)");
            p.action.x = to.x;
            p.action.y = to.y;
            p.action.z = to.z;
            p.SetValue(m.benefit, m.cost);

            // Backwards, facing what it fights (Backpedal.h): the pack follows without hitting the
            // tank's back. The forward walk is the fallback when no backwards path is found.
            if (Unit* victim = me->GetVictim())
            {
                Proposal back = p;
                back.action.type = BotActionType::Backpedal;
                back.action.target = victim->GetObjectGuid();
                back.action.holdMs = TS::BACKPEDAL_ORDER_MS;
                back.score += 1.0f;
                out.push_back(back);
            }
            p.action.holdMs = WalkHoldMs(me, distance);
            out.push_back(p);
        }

        // It walks to this tank by itself (header). Not while it casts: it stands until the cast
        // ends (strat-20261114-133225: an Eye of Naxxramas 10 yd off read its 10 s summon, the
        // tank waited for it to come and nobody hit it).
        bool Comes(EnemyFacts const& e, ObjectGuid tank)
        {
            return e.victim == tank && e.meleeEnabled && !e.rooted && !e.crowdControlled && !e.flee.fleeing &&
                   !e.castingSpellId && Tags::EngageOf(e) == Engage::Melee;
        }
    }

    bool TankStanceBehavior::AppliesTo(PartyBotAI const* bot) const
    {
        return bot->GetRole() == ROLE_TANK;
    }

    bool TankStanceBehavior::TakesOver(Takeover what, PartyBotAI const* bot, WorldModel const& world,
                                       TeamPlan const& plan) const
    {
        if (what != Takeover::Stance || !world.InFight(bot->me->IsInCombat()) || bot->IsInDuel() ||
            bot->me->GetTransport())
            return false;
        ObjectGuid const self = bot->me->GetObjectGuid();
        return plan.fightSpot.Moves(self) || self != plan.pull.tank || !plan.pull.PlacesTank();
    }

    void TankStanceBehavior::Propose(PartyBotAI const* bot, WorldModel const& world,
                                     TeamPlan const& plan, std::vector<Proposal>& out) const
    {
        // The retreat owns movement (Override tier).
        if (plan.retreat.active)
            return;
        Player* me = bot->me;
        if (me->HasUnitState(UNIT_STATE_ROOT) || bot->IsInDuel() || me->GetTransport())
            return;
        if (!world.InFight(me->IsInCombat()))
            return;
        PartyMemberFacts const* self = world.FindMember(me->GetObjectGuid());
        if (!self || !self->CanAct())
            return;

        if (plan.fightSpot.Moves(self->guid))
        {
            ProposeMove(bot, plan.fightSpot, out);
            return;
        }
        // The pull walks the tank (PullBehavior).
        if (self->guid == plan.pull.tank && plan.pull.PlacesTank())
            return;

        // The brain's target, else the victim.
        Unit* enemy = nullptr;
        if (TargetDuty const* duty = plan.FindTargetFor(self->guid))
            enemy = me->GetMap()->GetUnit(duty->enemy);
        if (!enemy || !enemy->IsAlive() || !bot->IsValidHostileTarget(enemy))
            enemy = me->GetVictim();
        if (!enemy || !enemy->IsAlive() || !bot->IsValidHostileTarget(enemy))
            return;

        float current = 0.0f;
        bool const chasing = bot->GetChaseAngle(enemy, current);
        if (me->CanReachWithMeleeAutoAttack(enemy))
            return;

        EnemyFacts const* facts = world.FindEnemy(enemy->GetObjectGuid());
        if (!facts)
            facts = world.FindTargeted(enemy->GetObjectGuid());
        bool const comes = facts && Comes(*facts, self->guid);
        if (comes || !plan.MayEngage(self->guid, enemy->GetObjectGuid()))
        {
            if (chasing)
                out.push_back(Make(ProposalTier::Assigned, BotActionType::StopChase,
                                   comes ? "it comes to the tank: hold the spot" : "off limits: hold the spot"));
            return;
        }
        if (chasing && std::fabs(current) < 0.01f)
            return;

        // Sent to wake it (TeamPlan::controlEnd): the walk is part of that order, not valued.
        // LostAction is 0 there when only controlled enemies are left (nothing hits the party),
        // and the arbiter dropped the chase: the tank stood 25 yd off for 30 s
        // (strat-20261114-132853).
        bool const wakes = plan.controlEnd.Breaks(self->guid, enemy->GetObjectGuid(), world.refreshedAtMs);
        Proposal p = Make(ProposalTier::Assigned, BotActionType::Chase,
                          wakes ? "sent to wake it: walk to it" : "it does not come: walk to it");
        p.action.target = enemy->GetObjectGuid();
        p.action.x = 0.0f;
        if (!wakes)
        {
            float const horizon = std::max(1.0f, std::min(HORIZON_SEC, Value::FightRemainingSec(world)));
            p.SetValue(Value::LostAction(world, *self, horizon), 0.0f);
        }
        out.push_back(p);
    }
}
