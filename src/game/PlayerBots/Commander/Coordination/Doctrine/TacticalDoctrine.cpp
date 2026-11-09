#include "Commander/Coordination/Doctrine/TacticalDoctrine.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/Profiles/Tags.h"
#include "Commander/Perception/Spatial/PackResolver.h"
#include "Commander/Perception/Tanks.h"
#include "Commander/Perception/WorldModel.h"
#include "Commander/Trace/Trace.h"
#include <algorithm>

namespace Commander
{
    namespace
    {
        using TD = TacticalDoctrine;

        // Not under a control (a fleeing enemy or one stunned while it flees is still hit).
        bool IsFree(EnemyFacts const& e)
        {
            return !e.control.spellId || e.HitWhileControlled();
        }

        // A bot holds it with a control the doctrine commits to.
        bool HeldByBot(WorldModel const& world, EnemyFacts const& e)
        {
            for (PartyMemberFacts const& m : world.party)
            {
                if (!m.isBot || m.isPet || !m.alive)
                    continue;
                for (ControlAbility const& a : m.controls)
                    if (a.oneTarget && a.holding == e.guid)
                        return true;
                if (e.control.spellId && e.control.caster == m.guid && e.control.breaksOnDamage)
                    return true;
            }
            return false;
        }

        bool InPull(TeamPlan const& plan)
        {
            return plan.pull.phase == PullPhase::Approach || plan.pull.phase == PullPhase::Return ||
                   plan.pull.phase == PullPhase::Settle;
        }

        bool Known(std::vector<ObjectGuid> const& known, ObjectGuid guid)
        {
            return std::find(known.begin(), known.end(), guid) != known.end();
        }
    }

    void TacticalDoctrine::Clear(TeamPlan& plan) const
    {
        plan.doctrine = DoctrinePlan();
    }

    void TacticalDoctrine::Assign(WorldModel const& world, TeamPlan& plan) const
    {
        DoctrinePlan const prev = plan.doctrine;
        DoctrinePlan& d = plan.doctrine;
        d = DoctrinePlan();
        if (!world.HasPartyEnemy())
            return;
        Trace::Scope scope("doctrine");

        std::vector<EnemyFacts const*> fight;
        uint32 free = 0;
        for (EnemyFacts const& e : world.enemies)
        {
            if (e.onAllyOnly || !e.acting || !e.health)
                continue;
            fight.push_back(&e);
            if (IsFree(e))
                ++free;
        }

        PartyMemberFacts const* tank = Tanks::Main(world);
        TeamLedger const ledger = Tags::LedgerOf(world);
        float const median = Tags::EliteMedianHealth(world);

        d.phase = InPull(plan) || plan.threat.active ? FightPhase::Gathering
                : free <= CLEANUP_FREE ? FightPhase::Cleanup : FightPhase::Clash;

        // Groups.
        uint32 freePile = 0;
        for (EnemyFacts const* e : fight)
        {
            DoctrineEnemy x;
            x.guid = e->guid;
            Engage const engage = Tags::EngageOf(*e);
            Tier const tier = Tags::TierOf(world, *e, median);
            x.engage = uint8(engage);
            x.tier = uint8(tier);
            if (tank)
            {
                bool const atTank = PackResolver::Distance(tank->pos, e->pos) - e->boundingRadius <= CLUSTER_YD;
                if (tier == Tier::Heavy && !atTank)
                    x.stamps |= STAMP_SPECIAL;
                else if (atTank || engage == Engage::Melee)
                    x.stamps |= STAMP_IN_CLUSTER;
                else
                    x.stamps |= STAMP_OUTLIER;
                if ((x.stamps & STAMP_IN_CLUSTER) && IsFree(*e))
                    ++freePile;
            }
            d.enemies.push_back(std::move(x));
        }

        // Tactic. The retreat: as RetreatAssigner left it (it runs after this module).
        if (!tank || plan.retreat.active)
        {
            d.tactic = PileTactic::None;
            d.why = !tank ? "no main tank" : "retreat";
        }
        else
        {
            char buf[160];
            std::string event;
            if (prev.tactic == PileTactic::None)
                event = "first choice";
            else if (d.phase == FightPhase::Gathering)
                event = "gathering";
            else if ((prev.areaDealers > 0) != (ledger.areaDealers > 0))
                event = ledger.areaDealers ? "an area dealer is back" : "no area dealer left";
            else if (prev.tactic == PileTactic::Area && freePile < AREA_KEEP)
            {
                snprintf(buf, sizeof(buf), "pile down to %u", freePile);
                event = buf;
            }
            else
                for (EnemyFacts const* e : fight)
                    if (!Known(prev.known, e->guid))
                    {
                        event = "add " + Trace::Guid(e->guid);
                        break;
                    }

            if (event.empty())
            {
                d.tactic = prev.tactic;
                d.sinceMs = prev.sinceMs;
                d.why = prev.why;
                d.areaDealers = prev.areaDealers;
                d.known = prev.known;
            }
            else
            {
                d.tactic = ledger.areaDealers > 0 && freePile >= AREA_MIN ? PileTactic::Area : PileTactic::Pick;
                d.sinceMs = d.tactic == prev.tactic && prev.sinceMs ? prev.sinceMs : world.refreshedAtMs;
                d.areaDealers = ledger.areaDealers;
                for (EnemyFacts const* e : fight)
                    d.known.push_back(e->guid);
                snprintf(buf, sizeof(buf), "%s: %u free in the pile, %u area dealer(s)",
                    event.c_str(), freePile, ledger.areaDealers);
                d.why = buf;
            }
        }

        // Stamps.
        for (DoctrineEnemy& x : d.enemies)
        {
            EnemyFacts const* e = world.FindEnemy(x.guid);
            if (!e)
                continue;
            auto noMez = [&x](char const* why)
            {
                x.stamps |= STAMP_NO_MEZ;
                if (x.noMezWhy.empty())
                    x.noMezWhy = why;
            };
            if (d.tactic == PileTactic::Area && (x.stamps & STAMP_IN_CLUSTER))
                noMez("area pile");
            if (Tier(x.tier) == Tier::Fodder)
                noMez("fodder");
            if (d.phase == FightPhase::Cleanup)
                noMez("cleanup");
            for (PartyMemberFacts const& m : world.party)
                if (!m.isBot && !m.isPet && m.alive && m.victim == e->guid)
                {
                    x.stamps |= STAMP_PLAYER_TARGET;
                    noMez("player hits it");
                }
            uint32 const othersFree = free - (IsFree(*e) ? 1 : 0);
            if (!(x.stamps & STAMP_NO_MEZ) && othersFree > 0 && HeldByBot(world, *e))
                x.stamps |= STAMP_COMMITTED;
        }

        if (scope.On())
        {
            scope.Label("%s, %s", Name(d.phase), Name(d.tactic));
            std::string list;
            for (DoctrineEnemy const& x : d.enemies)
                list += Trace::Format("%s{\"guid\":\"%s\",\"stamps\":%u,\"engage\":%u,\"tier\":%u}",
                    list.empty() ? "" : ",", Trace::Guid(x.guid).c_str(), x.stamps, x.engage, x.tier);
            scope.Data(Trace::Format("{\"why\":\"%s\",\"freePile\":%u,\"areaDealers\":%u,\"enemies\":[%s]}",
                d.why.c_str(), freePile, ledger.areaDealers, list.c_str()));
        }
    }

    char const* TacticalDoctrine::Name(FightPhase phase)
    {
        switch (phase)
        {
            case FightPhase::None:      return "no fight";
            case FightPhase::Gathering: return "gathering";
            case FightPhase::Clash:     return "clash";
            case FightPhase::Cleanup:   return "cleanup";
        }
        return "?";
    }

    char const* TacticalDoctrine::Name(PileTactic tactic)
    {
        switch (tactic)
        {
            case PileTactic::None: return "no tactic";
            case PileTactic::Area: return "area pile";
            case PileTactic::Pick: return "pick";
        }
        return "?";
    }

    std::string TacticalDoctrine::Describe(DoctrineEnemy const& d)
    {
        std::string s = std::string(Tags::Name(Engage(d.engage))) + ", " + Tags::Name(Tier(d.tier));
        if (d.stamps & STAMP_IN_CLUSTER)    s += ", in pile";
        if (d.stamps & STAMP_OUTLIER)       s += ", outlier";
        if (d.stamps & STAMP_SPECIAL)       s += ", special";
        if (d.stamps & STAMP_NO_MEZ)        s += ", no mez (" + d.noMezWhy + ")";
        if (d.stamps & STAMP_COMMITTED)     s += ", committed";
        if (d.stamps & STAMP_PLAYER_TARGET) s += ", player's target";
        return s;
    }
}
