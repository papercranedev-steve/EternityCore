#include "Commander/Behaviors/Movement/RunToTank.h"
#include "Commander/Behaviors/Movement/EscapeBehavior.h"
#include "Commander/Behaviors/Movement/FleePoint.h"
#include "Commander/BotDirectives.h"
#include "Commander/Perception/Spatial/SafeRoute.h"
#include "Commander/Perception/Tanks.h"
#include "Commander/Perception/Values/ValueModel.h"
#include "Commander/Perception/WorldModel.h"
#include "PartyBotAI.h"
#include "Timer.h"
#include <algorithm>
#include <cstring>

namespace Commander
{
namespace RunToTank
{
    using EB = EscapeBehavior;
    using SafeRoute::Distance2d;

    bool Propose(PartyBotAI const* bot, WorldModel const& world, PartyMemberFacts const& self,
                 std::vector<EnemyFacts const*> const& chasers, Unit* fleeFrom, float absorbedSec,
                 ProposalTier tier, char const* source, std::vector<Proposal>& out)
    {
        Player* me = bot->me;
        Position3 here;
        here.x = me->GetPositionX();
        here.y = me->GetPositionY();
        here.z = me->GetPositionZ();

        PartyMemberFacts const* tank = nullptr;
        for (PartyMemberFacts const* t : Tanks::Alive(world))
        {
            if (t->guid == self.guid || !t->CanAct())
                continue;
            if (!tank || Distance2d(here, t->pos) < Distance2d(here, tank->pos))
                tank = t;
        }
        if (!tank || chasers.empty())
            return false;
        float const distance = Distance2d(here, tank->pos);
        if (distance <= EB::START_DISTANCE || distance > EB::MAX_RUN_DISTANCE)
            return false;

        SafeRoute::IdleCreatures const idle(me);
        Position3 to;
        float const k = EB::STAND_OFF / distance;
        to.x = tank->pos.x + (here.x - tank->pos.x) * k;
        to.y = tank->pos.y + (here.y - tank->pos.y) * k;
        to.z = tank->pos.z;
        char const* reason = "melee enemy on me: run to the tank";
        if (idle.Near(to) || !SafeRoute::PathIsSafe(me, idle, to, EB::MAX_PATH_DETOUR))
        {
            // FleePoint prefers the tank too, else the party, else a ring; never an idle pack.
            if (!fleeFrom || !FleePoint::Find(me, fleeFrom, &world, to.x, to.y, to.z))
                return false;
            reason = "melee enemy on me: way to the tank blocked, run to a safe spot";
        }

        float const speed = std::max(1.0f, me->GetSpeed(MOVE_RUN));
        float const walkSec = Distance2d(here, to) * EB::PATH_FACTOR / speed;
        float benefit = 0.0f;
        for (EnemyFacts const* c : chasers)
        {
            float const horizon = std::max(1.0f, std::min(EB::HORIZON_SEC, Value::EnemyTimeToDie(world, *c)));
            // Loss of `seconds` more on me: the first absorbedSec of it costs nothing.
            auto const loss = [&](float seconds)
            {
                seconds = std::max(0.0f, seconds - absorbedSec);
                return Value::MoveLoss(world, *c, self, *tank, seconds) + Value::HarassLoss(world, *c, self, seconds);
            };
            benefit += loss(horizon) - loss(std::min(horizon, walkSec + EB::TAUNT_REACTION_SEC));
        }
        Proposal p;
        p.tier = tier;
        p.source = source;
        p.reason = reason;
        p.action.type = BotActionType::MoveTo;
        p.action.x = to.x;
        p.action.y = to.y;
        p.action.z = to.z;
        p.action.holdMs = uint32(walkSec * 1000.0f) + EB::HOLD_EXTRA_MS;
        p.SetValue(benefit, Value::LostAction(world, self, walkSec));
        out.push_back(p);
        return true;
    }

    bool ProposeStop(PartyBotAI const* bot, char const* source, char const* reason, std::vector<Proposal>& out)
    {
        if (!bot->me->IsMoving())
            return false;
        char const* owner = bot->Directives().GetMovementOwner(WorldTimer::getMSTime());
        if (!owner || strcmp(owner, source) != 0)
            return false;
        Proposal p;
        p.tier = ProposalTier::Assigned;
        p.source = source;
        p.reason = reason;
        // Above the "keep walking" Holds (score 0) other modules give for any MoveTo under
        // way: this walk is ours, they only keep it from being cut by a cast.
        p.score = 0.01f;
        p.action.type = BotActionType::StopMove;
        out.push_back(p);
        return true;
    }
}
}
