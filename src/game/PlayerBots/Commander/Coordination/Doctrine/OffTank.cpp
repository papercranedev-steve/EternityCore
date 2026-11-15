#include "Commander/Coordination/Doctrine/OffTank.h"
#include "Commander/Perception/Forecast/ThreatGap.h"
#include "Commander/Perception/Forecast/ThreatView.h"
#include "Commander/Perception/Spatial/PackResolver.h"
#include "Commander/Perception/Tanks.h"
#include "Commander/Perception/Values/SurvivalValue.h"
#include "Commander/Perception/Values/ValueModel.h"
#include "Commander/Trace/Trace.h"
#include <algorithm>
#include <cstdio>

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
            {
                h.seenOnMs = world.refreshedAtMs;
                h.takeByMs = 0;
            }
            else if (h.takeByMs)
            {
                if (world.refreshedAtMs > h.takeByMs)
                    return "the holder could not take it";
            }
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

        // Seconds `holder` needs to win `e` off its victim: 110% of the victim's threat at the
        // holder's threat rate, after the walk into melee reach.
        float WinSec(WorldModel const& world, EnemyFacts const& e, PartyMemberFacts const& holder)
        {
            ThreatEntry const* own = ThreatView::FindEntry(e, holder.guid);
            float const need = std::max(0.0f, e.victimThreat * ThreatView::MELEE_AGGRO_FACTOR - (own ? own->threat : 0.0f));
            float const walk = std::max(0.0f, PackResolver::Distance(holder.pos, e.pos) - e.boundingRadius - MELEE_REACH) /
                               RUN_YARDS_PER_SEC;
            return walk + need / ThreatGap::FillRate(world, holder);
        }

        // Header "Handed": the enemy on the main tank worth most to `holder`, if any.
        void Hand(WorldModel const& world, TeamPlan& plan, DoctrinePlan& d, PartyMemberFacts const& holder)
        {
            PartyMemberFacts const* tank = Tanks::Main(world);
            if (!tank || !tank->alive)
                return;
            std::vector<EnemyFacts const*> candidates;
            std::vector<float> winSecs;
            for (EnemyFacts const& e : world.enemies)
            {
                if (e.victim != tank->guid || d.OffTankOf(e.guid) || !MayTake(d, e) || plan.pull.target == e.guid ||
                    PackResolver::Distance(holder.pos, e.pos) > TANK_REACH)
                    continue;
                float const win = WinSec(world, e, holder);
                if (win > TAKE_MAX_SEC || Value::EnemyTimeToDie(world, e) < win + HOLD_MIN_SEC)
                    continue;
                candidates.push_back(&e);
                winSecs.push_back(win);
            }
            if (candidates.empty())
                return;

            Trace::Scope scope("offtank hand");
            FightLoad::Input asIs;
            if (!FightLoad::FromWorld(world, asIs))
                return;
            PutHeld(world, d, asIs);
            float const penalty = Value::DeathPenalty(*tank);
            float const base = FightLoad::Loss(world, FightLoad::Simulate(asIs, "off-tank: as is"), penalty);
            size_t best = candidates.size();
            float bestGain = MIN_GAIN;
            for (size_t i = 0; i < candidates.size(); ++i)
            {
                FightLoad::Input handed = asIs;
                if (!FightLoad::PutOn(world, handed, candidates[i]->guid, holder))
                    continue;
                float const gain = base - FightLoad::Loss(world, FightLoad::Simulate(handed, "off-tank: handed"), penalty);
                if (Trace::On())
                    Trace::Emit("hand", Trace::Guid(candidates[i]->guid).c_str(),
                        Trace::Format("{\"holder\":\"%s\",\"gain\":%.2f,\"winSec\":%.1f}", Trace::Guid(holder.guid).c_str(),
                            gain, winSecs[i]));
                if (gain > bestGain)
                {
                    bestGain = gain;
                    best = i;
                }
            }
            if (best == candidates.size())
                return;
            OffTankHold h;
            h.enemy = candidates[best]->guid;
            h.holder = holder.guid;
            h.sinceMs = h.seenOnMs = world.refreshedAtMs;
            h.takeByMs = world.refreshedAtMs + uint32((winSecs[best] + TAKE_MARGIN_SEC) * 1000.0f);
            h.points = bestGain;
            h.why = "the tank holds too much: handed over";
            d.offTanks.push_back(h);
            char buf[160];
            snprintf(buf, sizeof(buf), "Off-tank: %s takes one enemy off the tank (saves %.1f points).",
                holder.isPet ? "the pet" : "the off-tank", bestGain);
            plan.notes.push_back(buf);
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

    void PutHeld(WorldModel const& world, DoctrinePlan const& d, FightLoad::Input& fight)
    {
        for (OffTankHold const& h : d.offTanks)
            if (PartyMemberFacts const* m = world.FindMember(h.holder))
                FightLoad::PutOn(world, fight, h.enemy, *m);
    }

    void Apply(WorldModel const& world, TeamPlan& plan, DoctrinePlan const& prev, DoctrinePlan& d)
    {
        d.offTanks.clear();
        for (OffTankHold h : prev.offTanks)
        {
            char const* const why = DropWhy(world, h);
            if (Trace::On())
                Trace::Emit("offtank", Trace::Guid(h.enemy).c_str(),
                    Trace::Format("{\"holder\":\"%s\",\"kept\":%s,\"taking\":%s,\"why\":\"%s\"}", Trace::Guid(h.holder).c_str(),
                        why ? "false" : "true", h.takeByMs ? "true" : "false", why ? why : h.why.c_str()));
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
            // Still free: maybe the tank holds too much (header "Handed"). Not while gathering
            // (the pull: the tank first gets the pack).
            if (d.phase != FightPhase::Gathering && d.OffTankEnemyOf(m.guid).IsEmpty())
                Hand(world, plan, d, m);
        }

        for (DoctrineEnemy& x : d.enemies)
            if (d.OffTankOf(x.guid))
                x.stamps |= STAMP_OFF_TANK;
    }
}
}
