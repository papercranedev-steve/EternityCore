#include "Commander/Coordination/Doctrine/ControlledEnd.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/Forecast/FightLoad.h"
#include "Commander/Perception/Spatial/PackResolver.h"
#include "Commander/Perception/Spatial/PatrolZone.h"
#include "Commander/Perception/Tanks.h"
#include "Commander/Perception/WorldModel.h"
#include <algorithm>
#include <cstdio>
#include <string>

namespace Commander
{
    namespace
    {
        using CE = ControlledEndAssigner;

        // Area pile (header): the pile enemy held by a bot's control that breaks on damage,
        // nearest the main tank; null when none or no tank that can act.
        EnemyFacts const* PileToWake(WorldModel const& world, TeamPlan const& plan)
        {
            DoctrinePlan const& d = plan.doctrine;
            PartyMemberFacts const* tank = Tanks::Main(world);
            if (d.tactic != PileTactic::Area || d.phase != FightPhase::Clash || !tank || !tank->CanAct())
                return nullptr;
            EnemyFacts const* best = nullptr;
            for (DoctrineEnemy const& x : d.enemies)
            {
                if (!(x.stamps & STAMP_IN_CLUSTER))
                    continue;
                EnemyFacts const* e = world.FindEnemy(x.guid);
                if (!e || !e->control.spellId || !e->control.breaksOnDamage)
                    continue;
                PartyMemberFacts const* caster = world.FindMember(e->control.caster);
                if (!caster || !caster->isBot)
                    continue;   // a player's sheep is the player's choice
                if (!best || PackResolver::Distance(tank->pos, e->pos) < PackResolver::Distance(tank->pos, best->pos))
                    best = e;
            }
            return best;
        }

        struct WaitChoice
        {
            float sec = 0.0f;           // 0 = wake it now
            FightLoad::Result result;   // the fight after that wait
        };

        // Header "Waiting": none when the fight is bearable now; else the shortest wait (in
        // WAIT_STEP_SEC steps, at most maxSec) after which it is; else the longest one, if
        // that fight still goes better than now.
        WaitChoice ChooseWait(FightLoad::Input const& fight, FightLoad::Result const& now, float maxSec)
        {
            WaitChoice c;
            c.result = now;
            if (FightLoad::Bearable(now) || maxSec <= 0.0f)
                return c;
            for (float w = std::min(CE::WAIT_STEP_SEC, maxSec);; w = std::min(w + CE::WAIT_STEP_SEC, maxSec))
            {
                FightLoad::Result const r = FightLoad::Run(FightLoad::AfterWait(fight, w), nullptr);
                bool const last = w >= maxSec;
                if (FightLoad::Bearable(r) || (last && FightLoad::Score(r) > FightLoad::Score(now)))
                {
                    c.sec = w;
                    c.result = r;
                    break;
                }
                if (last)
                    break;
            }
            if (c.sec > 0.0f)   // for the calculation trace
                FightLoad::Simulate(FightLoad::AfterWait(fight, c.sec), "controlled end: after the wait");
            return c;
        }
    }

    void ControlledEndAssigner::Clear(TeamPlan& plan) const
    {
        plan.controlEnd = ControlEndPlan();
    }

    void ControlledEndAssigner::Assign(WorldModel const& world, TeamPlan& plan) const
    {
        ControlEndPlan& end = plan.controlEnd;

        // The doctrine's area pile (header: "Area pile").
        if (EnemyFacts const* held = PileToWake(world, plan))
        {
            PartyMemberFacts const* tank = Tanks::Main(world);
            if (end.active && end.enemy == held->guid && end.breaker == tank->guid)
                return;
            end = ControlEndPlan();
            end.active = true;
            end.enemy = held->guid;
            end.breaker = tank->guid;
            end.breakAtMs = world.refreshedAtMs;
            end.why = "area pile: wake the controlled one";
            plan.notes.push_back("Area pile: the tank wakes the controlled enemy in it, so the area spell takes it too.");
            end.noted = true;
            return;
        }

        EnemyFacts const* first = nullptr;
        bool any = false;
        for (EnemyFacts const& e : world.enemies)
        {
            if (e.onAllyOnly || !e.acting || !e.health)
                continue;
            any = true;
            // Someone is still fighting, or held by something the party may hit through (a
            // stun, a fear): not this situation.
            if (!e.control.spellId || !e.control.breaksOnDamage)
            {
                end = ControlEndPlan();
                return;
            }
            if (!first || e.control.remainingMs < first->control.remainingMs)
                first = &e;
        }
        PartyMemberFacts const* tank = Tanks::Main(world);
        if (!any || !first || !tank || !tank->CanAct())
        {
            end = ControlEndPlan();
            return;
        }
        // Decided for this enemy already: keep it.
        if (end.active && end.enemy == first->guid && end.breaker == tank->guid)
            return;

        end = ControlEndPlan();
        end.active = true;
        end.enemy = first->guid;
        end.breaker = tank->guid;

        float const walkSec = std::max(0.0f, PackResolver::Distance(tank->pos, first->pos) - first->boundingRadius) / RUN_YD_PER_SEC;
        float const maxWaitSec = std::max(0.0f, first->control.remainingMs / 1000.0f - BREAK_LEAD_SEC - walkSec);

        // The fight still to come: every controlled enemy, all at once (rough: they wake one by one).
        FightLoad::Input fight;
        if (!FightLoad::FromWorld(world, fight))
        {
            plan.notes.push_back("Only controlled enemies left: the tank wakes the first now.");
            end.noted = true;
            return;
        }
        for (FightLoad::Foe& f : fight.foes)
        {
            f.arriveSec = 0.0f;
            f.sameFight = false;
        }
        FightLoad::Result const now = FightLoad::Simulate(fight, "controlled end: wake now");
        WaitChoice const choice = ChooseWait(fight, now, maxWaitSec);
        bool const patrol = choice.sec > 0.0f &&
            PatrolZone::Comes(world, first->pos, choice.sec + choice.result.fightSec);
        bool const wait = choice.sec > 0.0f && !patrol;
        end.breakAtMs = world.refreshedAtMs + (wait ? uint32(choice.sec * 1000.0f) : 0);

        char buf[300];
        std::string const nowText = FightLoad::Describe(now);
        if (wait)
            snprintf(buf, sizeof(buf), "Only controlled enemies left: woken now it is too hard (%s); waiting %.0f s for mana (%s), then the tank wakes the first.",
                nowText.c_str(), choice.sec, FightLoad::Describe(choice.result).c_str());
        else if (patrol)
            snprintf(buf, sizeof(buf), "Only controlled enemies left: the tank wakes the first now, a patrol is coming (%s).", nowText.c_str());
        else
            snprintf(buf, sizeof(buf), "Only controlled enemies left: the tank wakes the first now (%s).", nowText.c_str());
        plan.notes.push_back(buf);
        end.noted = true;
    }
}
