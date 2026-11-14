#include "Commander/Behaviors/Movement/JoinGamble.h"
#include "Commander/Perception/Spatial/SafeRoute.h"
#include "Commander/Perception/Values/JoinRisk.h"
#include "Commander/Perception/Values/SpotCost.h"
#include "Commander/Perception/Values/ValueModel.h"
#include "Commander/Trace/Trace.h"
#include "Creature.h"
#include "Player.h"
#include <algorithm>

namespace Commander
{
namespace JoinGamble
{
    bool Propose(Player* me, WorldModel const& world, PartyMemberFacts const& self, SpotRejects const& why,
                 Position3 const& fightAt, char const* source, std::vector<Proposal>& out)
    {
        Creature const* blocker = why.blockerUnit;
        if (!blocker || !blocker->IsAlive() || blocker->IsInCombat())
            return false;
        float const loss = JoinRisk::Loss(world, blocker);
        if (loss < 0.0f)
            return false;

        Position3 here;
        here.x = me->GetPositionX();
        here.y = me->GetPositionY();
        here.z = me->GetPositionZ();
        Position3 const& to = why.blockedAt;
        float const speed = std::max(1.0f, me->GetSpeed(MOVE_RUN));
        float const walkSec = SafeRoute::Distance2d(here, to) * PATH_FACTOR / speed;
        float const horizon = std::max(1.0f, std::min(JoinRisk::HORIZON_SEC, Value::FightRemainingSec(world)));
        float const benefit = Value::LostAction(world, self, horizon);
        float const cost = loss + SpotCost::Walk(world, self, walkSec);
        bool const pays = benefit > cost;

        bool clear = false;
        if (pays)
        {
            SafeRoute::IdleCreatures idle(me, nullptr, SafeRoute::TIGHT_MARGIN);
            idle.Ignore(JoinRisk::Joiners(world, blocker->GetObjectGuid()));
            SafeRoute::IdleCreatures const others = idle.ArrivingAt(fightAt);
            clear = !others.Near(to) && SafeRoute::PathIsSafe(me, others, to, MAX_PATH_DETOUR);
        }
        if (Trace::On())
            Trace::Emit("stance", Trace::Format("%s: walk past %s? benefit %.1f, its join %.1f + walk%s", me->GetName(),
                                                blocker->GetName(), benefit, loss, !pays ? ": not worth it" : clear ? ": go" : ": no way past"),
                        Trace::Format("{\"bot\":\"%s\",\"blocker\":\"%s\",\"benefit\":%.2f,\"join\":%.2f,\"cost\":%.2f,\"clear\":%s}",
                                      Trace::Guid(me->GetObjectGuid()).c_str(), Trace::Guid(blocker->GetObjectGuid()).c_str(),
                                      benefit, loss, cost, clear ? "true" : "false"));
        if (!pays || !clear)
            return false;

        Proposal p;
        p.tier = ProposalTier::Assigned;
        p.source = source;
        p.reason = "no safe way: walk past an idle creature, its join is worth it";
        p.action.type = BotActionType::MoveTo;
        p.action.x = to.x;
        p.action.y = to.y;
        p.action.z = to.z;
        p.action.holdMs = uint32(walkSec * 1000.0f) + HOLD_EXTRA_MS;
        p.SetValue(benefit, cost);
        out.push_back(p);
        return true;
    }
}
}
