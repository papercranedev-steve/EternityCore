#include "Commander/Coordination/Situation/Crisis/WipeAssigner.h"
#include "Commander/Coordination/Duties/SoulstoneAssigner.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/Forecast/FightLoad.h"
#include "Timer.h"
#include <cstdio>

namespace Commander
{
    namespace
    {
        // Header "Called": is the fight lost right now (before the confirmation time)?
        bool Losing(WorldModel const& world, TeamPlan const& plan, std::string& why)
        {
            CrisisPlan const& c = plan.crisis;
            if (c.lost)
            {
                if (c.best < 0)
                {
                    why = "nobody can carry the fight";
                    return true;
                }
                if (!c.options[c.best].bearable)
                {
                    why = "even the best way on loses";
                    return true;
                }
                return false;
            }
            if (!plan.emergencyRoles.empty())
            {
                FightLoad::Input in;
                if (FightLoad::FromWorld(world, in) && !in.foes.empty() && FightLoad::Simulate(in, "wipe").tankDies)
                {
                    why = "the stand-in will not hold";
                    return true;
                }
            }
            return false;
        }

        bool Has(std::vector<ObjectGuid> const& v, ObjectGuid g)
        {
            return std::find(v.begin(), v.end(), g) != v.end();
        }

        // Header "stone": who gets the Soulstone, empty = nobody fits. The out-of-combat
        // stone's order (SoulstoneAssigner::TargetRank), only members who resurrect the others.
        ObjectGuid StoneTarget(WorldModel const& world)
        {
            PartyMemberFacts const* best = nullptr;
            int bestRank = 0;
            for (PartyMemberFacts const& m : world.party)
            {
                int const rank = SoulstoneAssigner::TargetRank(m);
                if (rank < SoulstoneAssigner::RESURRECTS_RANK)
                    continue;
                if (rank > bestRank)
                {
                    best = &m;
                    bestRank = rank;
                }
            }
            return best ? best->guid : ObjectGuid();
        }

        std::string Note(WipePlan const& w)
        {
            std::string text = "We lose this one (" + w.why + ").";
            if (!w.revivers.empty())
                text += w.revivers.size() == 1 ? " One of us rises by himself afterwards and resurrects the rest: "
                                                 "do not release, wait for the resurrection."
                                               : " Some of us rise by themselves afterwards and resurrect the rest: "
                                                 "do not release, wait for the resurrection.";
            else if (!w.stoneBy.IsEmpty())
                text += " The warlock puts a Soulstone on someone who resurrects: do not release, wait for it.";
            else
                text += " Nobody can rise by himself: release once it is over.";
            if (!w.hiders.empty())
            {
                char buf[96];
                snprintf(buf, sizeof(buf), " %u drop out of the fight (Feign Death, Vanish).", uint32(w.hiders.size()));
                text += buf;
            }
            return text;
        }

        std::string KeyOf(WipePlan const& w)
        {
            return std::to_string(w.revivers.size()) + "|" + std::to_string(w.stoneBy.GetRawValue()) + "|" +
                   std::to_string(w.hiders.size());
        }
    }

    void WipeAssigner::Assign(WorldModel const& world, TeamPlan& plan) const
    {
        WipePlan& w = plan.wipe;
        if (!world.HasPartyEnemy())
        {
            w = WipePlan();
            return;
        }
        if (!w.active)
        {
            std::string why;
            if (plan.retreat.declineNoted)
            {
                w.active = true;
                w.why = "you ran and there is no way out";
            }
            else if (Losing(world, plan, why))
            {
                if (!w.losingSinceMs)
                    w.losingSinceMs = world.refreshedAtMs;
                if (WorldTimer::getMSTimeDiff(w.losingSinceMs, world.refreshedAtMs) >= LOSE_CONFIRM_MS)
                {
                    w.active = true;
                    w.why = why;
                }
            }
            else
                w.losingSinceMs = 0;
            if (!w.active)
                return;
            w.sinceMs = world.refreshedAtMs;
        }

        w.revivers.clear();
        for (PartyMemberFacts const& m : world.party)
            if (!m.isPet && m.selfRes && m.resurrects)
                w.revivers.push_back(m.guid);

        w.stoneBy = ObjectGuid();
        w.stoneOn = ObjectGuid();
        w.stoneItem = 0;
        if (w.revivers.empty())
        {
            ObjectGuid const on = StoneTarget(world);
            for (PartyMemberFacts const& m : world.party)
                if (!on.IsEmpty() && m.isBot && !m.isPet && m.CanAct() && m.soulstoneItem)
                {
                    w.stoneBy = m.guid;
                    w.stoneOn = on;
                    w.stoneItem = m.soulstoneItem;
                    break;
                }
        }

        // Hiders: kept once hidden (their drop is on cooldown then), new ones with one ready.
        std::vector<WipeHider> hiders;
        for (PartyMemberFacts const& m : world.party)
        {
            if (!m.isBot || m.isPet || !m.alive || Has(w.revivers, m.guid) || m.guid == w.stoneBy)
                continue;
            if (w.Hides(m.guid))
            {
                for (WipeHider const& h : w.hiders)
                    if (h.bot == m.guid)
                        hiders.push_back(h);
                continue;
            }
            for (ThreatDropAbility const& d : m.threatDrops)
                if (d.wipe && d.ready)
                {
                    hiders.push_back({ m.guid, d.spellId });
                    break;
                }
        }
        w.hiders = hiders;

        std::string const key = KeyOf(w);
        if (key != w.notedKey)
        {
            w.notedKey = key;
            plan.notes.push_back(Note(w));
        }
    }

    void WipeAssigner::Clear(TeamPlan& plan) const
    {
        plan.wipe = WipePlan();
    }
}
