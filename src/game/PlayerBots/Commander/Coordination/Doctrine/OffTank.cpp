#include "Commander/Coordination/Doctrine/OffTank.h"
#include "Commander/Perception/Tanks.h"
#include "Commander/Perception/Values/SurvivalValue.h"
#include "Commander/Trace/Trace.h"

namespace Commander
{
namespace OffTank
{
    namespace
    {
        uint32 const NOT_TAKEN = STAMP_KILL_NOW | STAMP_KILL_ORDER | STAMP_FREE_ALLY | STAMP_SELF_DESTRUCT |
                                 STAMP_SWARM | STAMP_NO_HOLD | STAMP_HOLD_FIRE | STAMP_PLAYER_TARGET;

        // Why `h` is no longer held, or null.
        char const* DropWhy(WorldModel const& world, OffTankHold& h)
        {
            EnemyFacts const* e = world.FindEnemy(h.enemy);
            if (!e || !e->health)
                return "it died";
            if (!e->acting || e->crowdControlled)
                return "it is out of the fight";
            PartyMemberFacts const* m = world.FindMember(h.holder);
            if (!m || !IsHolder(world, *m))
                return "the holder cannot hold";
            if (e->victim == h.holder)
                h.seenOnMs = world.refreshedAtMs;
            else if (world.refreshedAtMs - h.seenOnMs > LOST_MS)
                return "the holder lost it";
            if (SurvivalValue::Of(world, *m).InDanger())
                return "the holder is about to die";
            return nullptr;
        }

        // The main tank's own enemy (header "Taken"): its victim, the pull target.
        bool MainTanks(WorldModel const& world, TeamPlan const& plan, EnemyFacts const& e)
        {
            PartyMemberFacts const* tank = Tanks::Main(world);
            return (tank && tank->victim == e.guid) || plan.pull.target == e.guid;
        }

        bool HeavyOfPile(DoctrinePlan const& d, DoctrineEnemy const& x)
        {
            return x.group >= 0 && size_t(x.group) < d.groups.size() && d.groups[x.group].heavy == x.guid;
        }
    }

    bool IsHolder(WorldModel const& world, PartyMemberFacts const& m)
    {
        return m.canOffTank && m.CanAct() && Tanks::Main(world) != nullptr;
    }

    bool IsFree(WorldModel const& world, DoctrinePlan const& d, PartyMemberFacts const& m)
    {
        return IsHolder(world, m) && m.healthPct >= ADOPT_MIN_PCT && d.OffTankEnemyOf(m.guid).IsEmpty() &&
               !SurvivalValue::Of(world, m).InDanger();
    }

    bool MayTake(DoctrinePlan const& d, EnemyFacts const& e)
    {
        if (!e.acting || e.crowdControlled || !e.health)
            return false;
        DoctrineEnemy const* x = d.Find(e.guid);
        return x && !(x->stamps & NOT_TAKEN) && !HeavyOfPile(d, *x);
    }

    void Apply(WorldModel const& world, TeamPlan const& plan, DoctrinePlan const& prev, DoctrinePlan& d)
    {
        d.offTanks.clear();
        for (OffTankHold h : prev.offTanks)
        {
            char const* const why = DropWhy(world, h);
            if (Trace::On())
                Trace::Emit("offtank", Trace::Guid(h.enemy).c_str(),
                    Trace::Format("{\"holder\":\"%s\",\"kept\":%s,\"why\":\"%s\"}", Trace::Guid(h.holder).c_str(),
                        why ? "false" : "true", why ? why : h.why.c_str()));
            if (!why)
                d.offTanks.push_back(h);
        }

        for (PartyMemberFacts const& m : world.party)
        {
            if (!IsFree(world, d, m))
                continue;
            for (DoctrineEnemy const& x : d.enemies)
            {
                EnemyFacts const* e = world.FindEnemy(x.guid);
                if (!e || e->victim != m.guid || d.OffTankOf(e->guid) || !MayTake(d, *e))
                    continue;
                RescueDuty const* rescue = plan.FindRescueOf(e->guid);
                bool const given = rescue && rescue->bot == m.guid;
                if (!given && MainTanks(world, plan, *e))
                    continue;
                OffTankHold h;
                h.enemy = e->guid;
                h.holder = m.guid;
                h.sinceMs = h.seenOnMs = world.refreshedAtMs;
                h.why = given ? "picked it up (rescue)" : "it hits the off-tank";
                d.offTanks.push_back(h);
                break;
            }
        }

        for (DoctrineEnemy& x : d.enemies)
            if (d.OffTankOf(x.guid))
                x.stamps |= STAMP_OFF_TANK;
    }
}
}
