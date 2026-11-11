#include "Commander/Coordination/Doctrine/ControlledEnd.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/Forecast/FightLoad.h"
#include "Commander/Perception/Values/ManaWorth.h"
#include "Commander/Perception/Spatial/PackResolver.h"
#include "Commander/Perception/Spatial/PatrolZone.h"
#include "Commander/Perception/Tanks.h"
#include "Commander/Perception/WorldModel.h"
#include <algorithm>
#include <cstdio>

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
        float const waitSec = std::max(0.0f, first->control.remainingMs / 1000.0f - BREAK_LEAD_SEC - walkSec);

        // The fight still to come: every controlled enemy, all at once (rough: they wake one by one).
        FightLoad::Input fight;
        ManaWorth::Fight planned;
        planned.drinkAfter = false;     // header: only mana this fight lacks
        if (FightLoad::FromWorld(world, fight))
        {
            for (FightLoad::Foe& f : fight.foes)
            {
                f.arriveSec = 0.0f;
                f.sameFight = false;
            }
            FightLoad::Result const r = FightLoad::Simulate(fight, "controlled end: the rest of the fight");
            planned.sec = r.fightSec;
            planned.incomingDps = r.damage / std::max(1.0f, r.fightSec);
        }

        for (PartyMemberFacts const& m : world.party)
        {
            if (m.isPet || !m.alive || !m.UsesMana() || m.manaRegen <= 0.0f)
                continue;
            float const regained = std::min(m.manaRegen * waitSec, float(m.maxMana) - float(m.mana));
            if (regained > 0.0f)
                end.waitGain += ManaWorth::Points(world, m, regained, planned);
        }
        end.waitCost = WAIT_POINTS_PER_SEC * waitSec;
        bool const patrol = waitSec > 0.0f && PatrolZone::Comes(world, first->pos, waitSec + planned.sec);
        bool const wait = waitSec > 0.0f && !patrol && end.waitGain > end.waitCost;
        end.breakAtMs = world.refreshedAtMs + (wait ? uint32(waitSec * 1000.0f) : 0);

        char buf[200];
        if (wait)
            snprintf(buf, sizeof(buf), "Only controlled enemies left: waiting %.0f s for mana (%.1f pt vs %.1f), then the tank wakes the first.",
                waitSec, end.waitGain, end.waitCost);
        else
            snprintf(buf, sizeof(buf), "Only controlled enemies left: the tank wakes the first now%s.",
                patrol ? " (a patrol is coming)" : "");
        plan.notes.push_back(buf);
        end.noted = true;
    }
}
