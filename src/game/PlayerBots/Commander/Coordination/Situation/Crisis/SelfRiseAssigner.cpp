#include "Commander/Coordination/Situation/Crisis/SelfRiseAssigner.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/Forecast/FightLoad.h"
#include "Commander/Perception/Tanks.h"
#include <algorithm>
#include <cstdio>

namespace Commander
{
    namespace
    {
        struct Run
        {
            bool ran = false;
            bool bearable = false;
            float score = -1000.0f;     // cannot be run: lost
            std::string text = "no tank";
        };

        Run RunOf(WorldModel const& world, FightLoad::Lineup const& lineup)
        {
            Run r;
            FightLoad::Input in;
            if (!FightLoad::FromWorld(world, lineup, in) || in.foes.empty())
                return r;
            FightLoad::Result const res = FightLoad::Simulate(in, "self rise");
            r.ran = true;
            r.bearable = FightLoad::Bearable(res);
            r.score = FightLoad::Score(res);
            r.text = FightLoad::Describe(res);
            return r;
        }

        // Header: an enemy not on a tank right next to the body.
        bool InDanger(WorldModel const& world, PartyMemberFacts const& dead)
        {
            for (EnemyFacts const& e : world.enemies)
            {
                if (e.onAllyOnly || !e.acting || e.control.spellId)
                    continue;
                PartyMemberFacts const* victim = world.FindMember(e.victim);
                if (victim && Tanks::IsTank(*victim))
                    continue;
                float const dx = e.pos.x - dead.pos.x, dy = e.pos.y - dead.pos.y;
                if (dx * dx + dy * dy <= SelfRiseAssigner::DANGER_YD * SelfRiseAssigner::DANGER_YD)
                    return true;
            }
            return false;
        }
    }

    void SelfRiseAssigner::Assign(WorldModel const& world, TeamPlan& plan) const
    {
        if (!world.HasPartyEnemy())
        {
            plan.rises.clear();
            return;
        }
        Run const asIs = RunOf(world, FightLoad::Lineup());
        for (PartyMemberFacts const& m : world.party)
        {
            if (!m.isBot || m.isPet || m.alive || !m.selfRes || (m.role != ROLE_TANK && m.role != ROLE_HEALER) ||
                std::find(plan.rises.begin(), plan.rises.end(), m.guid) != plan.rises.end())
                continue;
            FightLoad::Lineup lineup;
            lineup.risen.push_back(m.guid);
            if (m.role == ROLE_TANK)
                lineup.tank = m.guid;
            Run const risen = RunOf(world, lineup);
            // Only to turn a lost fight: a fight won anyway keeps the stone for afterwards.
            if (!risen.ran || !risen.bearable || asIs.bearable)
                continue;
            if (InDanger(world, m))
                continue;
            plan.rises.push_back(m.guid);
            if (plan.wipe.active)
                plan.wipe = WipePlan();     // not lost any more
            plan.wipe.losingSinceMs = 0;
            plan.notes.push_back(std::string("Our ") + (m.role == ROLE_TANK ? "tank" : "healer") +
                " rises now (its own resurrection): with it we can win (" + risen.text + "; without: " +
                asIs.text + ").");
            return;     // one per refresh
        }
    }

    void SelfRiseAssigner::Clear(TeamPlan& plan) const
    {
        plan.rises.clear();
    }
}
