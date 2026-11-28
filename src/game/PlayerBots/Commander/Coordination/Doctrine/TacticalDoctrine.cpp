#include "Commander/Coordination/Doctrine/TacticalDoctrine.h"
#include "Commander/Coordination/Doctrine/Battlefield.h"
#include "Commander/Coordination/Doctrine/Stragglers.h"
#include "Commander/Coordination/Doctrine/OffTank.h"
#include "Commander/Coordination/Doctrine/TacticCheck.h"
#include "Commander/Coordination/Situation/Patrol/RouteGuard.h"
#include "Commander/Coordination/Doctrine/FightPlace.h"
#include "Commander/Coordination/Doctrine/RoleStamps.h"
#include "Commander/Coordination/Doctrine/AllyCharm.h"
#include "Commander/Coordination/Doctrine/AreaIntent.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/Profiles/ProfileStore.h"
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

        // The pull's creatures as the pack scan saw them (they are idle until the fight).
        std::vector<Battlefield::Unit> PullUnits(WorldModel const& world, std::vector<ObjectGuid> const& expected)
        {
            std::vector<Battlefield::Unit> out;
            auto const add = [&out](PackCreature const& c)
            {
                Battlefield::Unit u;
                u.guid = c.guid;
                u.entry = c.entry;
                u.pos = c.pos;
                u.elite = c.elite;
                u.maxHealth = c.maxHealth;
                out.push_back(u);
            };
            for (ObjectGuid g : expected)
            {
                bool found = false;
                for (PackFacts const& p : world.packs)
                    for (PackCreature const& c : p.members)
                        if (!found && c.guid == g)
                        {
                            add(c);
                            found = true;
                        }
                for (PatrolFacts const& p : world.patrols)
                    if (!found && p.guid == g)
                    {
                        add(p);
                        found = true;
                    }
            }
            return out;
        }

        std::string NameOf(WorldModel const& world, ObjectGuid guid)
        {
            uint32 entry = 0;
            if (EnemyFacts const* e = world.FindEnemy(guid))
                entry = e->entry;
            for (PackFacts const& p : world.packs)
                for (PackCreature const& c : p.members)
                    if (!entry && c.guid == guid)
                        entry = c.entry;
            for (PatrolFacts const& p : world.patrols)
                if (!entry && p.guid == guid)
                    entry = p.entry;
            std::shared_ptr<CreatureProfile const> profile = entry ? sProfileStore.GetCreature(entry) : nullptr;
            return profile ? profile->name : Trace::Guid(guid);
        }

        // "pile, 3: area pile (Ghoul x3)" for the party chat.
        std::string Says(WorldModel const& world, DoctrinePlan const& d, DoctrineGroup const& g)
        {
            std::vector<std::pair<std::string, uint32>> names;
            for (ObjectGuid m : g.members)
            {
                std::string const n = NameOf(world, m);
                auto it = std::find_if(names.begin(), names.end(), [&n](auto const& p) { return p.first == n; });
                if (it == names.end())
                    names.push_back({ n, 1 });
                else
                    ++it->second;
            }
            std::string list;
            for (auto const& n : names)
                list += (list.empty() ? "" : ", ") + n.first + (n.second > 1 ? " x" + std::to_string(n.second) : "");
            return Battlefield::Describe(d, g) + " (" + list + ")";
        }

        // Same kinds and members (not the arrival times, they change every refresh).
        bool SameSplit(std::vector<DoctrineGroup> const& a, std::vector<DoctrineGroup> const& b)
        {
            if (a.size() != b.size())
                return false;
            for (size_t i = 0; i < a.size(); ++i)
                if (a[i].kind != b[i].kind || a[i].members != b[i].members)
                    return false;
            return true;
        }

        // A pull is planned and nobody fights yet: the groups of the planned fight (Battlefield.h),
        // made again every refresh until the fight starts (the pull is predicted again on its way).
        // Said in the party chat when the split changes.
        void PlanGroups(WorldModel const& world, TeamPlan& plan, DoctrinePlan const& prev, DoctrinePlan& d)
        {
            Trace::Scope scope("doctrine");
            PullPlan const& pull = plan.pull;
            d.phase = FightPhase::Planned;
            d.why = "groups of the planned pull";
            d.groups = Battlefield::FromPull(world, PullUnits(world, pull.expected), pull.target, world.refreshedAtMs,
                prev.phase == FightPhase::Planned ? &prev.groups : nullptr);
            Position3 spot;
            if (plan.PlannedFightSpot(spot))
                Battlefield::AddPatrols(world, spot, RouteGuard::StaySec(world, plan), pull.expected,
                    world.refreshedAtMs, d.groups);
            // The patrol the pull holds when it comes (PullPlan::holdPatrol, PullPatrolCheck): its
            // group is held outside the pile, the hold is the controller's task in the fight.
            int const held = pull.holdPatrol.IsEmpty() ? -1 : Battlefield::Find(d.groups, pull.holdPatrol);
            if (held >= 0)
            {
                d.groups[held].holdOut = true;
                DoctrineHold h;
                h.enemy = pull.holdPatrol;
                h.bot = pull.holdBot;
                h.spellId = pull.holdSpellId;
                h.planned = true;
                h.why = "the pull's plan: held when it comes";
                if (PartyMemberFacts const* b = world.FindMember(h.bot))
                    for (ControlAbility const& a : b->controls)
                        if (a.spellId == h.spellId)
                            h.kind = a.Kind();
                d.holds.push_back(h);
            }
            if (!d.groups.empty() && !SameSplit(prev.groups, d.groups))
            {
                std::string list;
                for (DoctrineGroup const& g : d.groups)
                    list += (list.empty() ? "" : "; ") + Says(world, d, g);
                plan.notes.push_back("Plan: " + list + ".");
            }
            if (scope.On())
            {
                scope.Label("planned: %u group(s)", uint32(d.groups.size()));
                std::string list;
                for (DoctrineGroup const& g : d.groups)
                    list += Trace::Format("%s\"%s\"", list.empty() ? "" : ",", Battlefield::Describe(d, g).c_str());
                scope.Data(Trace::Format("{\"groups\":[%s]}", list.c_str()));
            }
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
        {
            if (plan.pull.phase != PullPhase::None)
                PlanGroups(world, plan, prev, d);
            return;
        }
        Trace::Scope scope("doctrine");
        // The groups of the pull's plan or of the refreshes before (Battlefield.h).
        d.groups = prev.groups;
        bool const byPlaceOnly = d.groups.empty();
        // A reselect event for the check (TacticCheck.h): the first refresh of the fight, or an
        // enemy the last refresh did not know (prev.known is the last refresh's while gathering,
        // the last choice's after it: an add is an event then too).
        bool recheck = prev.tactic == PileTactic::None;
        for (EnemyFacts const& e : world.enemies)
            if (!recheck && !e.onAllyOnly && e.acting && e.health && !Known(prev.known, e.guid))
                recheck = true;

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

        d.phase = InPull(plan) ? FightPhase::Gathering
                : free <= CLEANUP_FREE ? FightPhase::Cleanup : FightPhase::Clash;

        // Groups.
        uint32 freePile = 0;
        std::vector<std::pair<ObjectGuid, int>> cameIn;     // not in the plan, placed this refresh
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
                int g = Battlefield::Find(d.groups, e->guid);
                if (g < 0)
                {
                    g = Battlefield::Place(world, *e, *tank, byPlaceOnly, world.refreshedAtMs, d.groups);
                    if (!byPlaceOnly)
                        cameIn.push_back({ e->guid, g });
                }
                x.group = int8(g);
                bool const joined = Battlefield::UpdateJoined(*e, *tank, d.groups[g]);
                x.stamps |= Battlefield::StampOf(d.groups[g].kind, joined, d.groups[g].holdOut, engage, tier);
                if ((x.stamps & STAMP_IN_CLUSTER) && IsFree(*e))
                    ++freePile;
            }
            d.enemies.push_back(std::move(x));
        }
        // One party chat line for what came in unplanned this refresh:
        // "Came in (not planned): Scarlet Monk x3 (melee stragglers (add 1)), Patchwork Horror (patrol)."
        if (!cameIn.empty())
        {
            std::vector<std::pair<std::string, uint32>> seen;
            for (auto const& c : cameIn)
            {
                DoctrineGroup const& g = d.groups[c.second];
                std::string label = Battlefield::Name(g.kind);
                if (g.addBatch)
                    label += " (add " + std::to_string(g.addBatch) + ")";
                std::string const key = NameOf(world, c.first) + "|" + label;
                auto it = std::find_if(seen.begin(), seen.end(), [&key](auto const& p) { return p.first == key; });
                if (it == seen.end())
                    seen.push_back({ key, 1 });
                else
                    ++it->second;
            }
            std::string list;
            for (auto const& s : seen)
            {
                size_t const bar = s.first.find('|');
                list += (list.empty() ? "" : ", ") + s.first.substr(0, bar) +
                        (s.second > 1 ? " x" + std::to_string(s.second) : "") + " (" + s.first.substr(bar + 1) + ")";
            }
            plan.notes.push_back("Came in (not planned): " + list + ".");
        }

        // Tactic. The retreat: as RetreatAssigner left it (it runs after this module).
        if (!tank || plan.retreat.active)
        {
            d.tactic = PileTactic::None;
            // (a team with nobody to hold, TeamPlan::coverage: no pile to place, each member
            // fights what hits it; doc 2026-11-19-any-size-team.md)
            d.why = plan.retreat.active ? "retreat" :
                    plan.coverage.noHolder ? "nobody holds: each fights what hits it" : "no main tank";
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

        // The main tank's job (TankJob): HoldPile under Area, else none (the tank follows the
        // threat risk, TankTargets).
        d.tankJob = d.phase != FightPhase::Gathering && d.tactic == PileTactic::Area ? TankJob::HoldPile : TankJob::None;
        // The area spell the tactic counts on: the tank's runway is judged with it (AreaIntent.h).
        if (d.tactic == PileTactic::Area)
            AreaIntent::Fill(world, d);

        // The enemies away from the tank (FightPlace.h). The pull's line of sight break as its map
        // step (PullPlanner::SearchLosBreak, after the assigners) left it last refresh; it reads
        // this refresh's lure stamps.
        if (tank && !plan.retreat.active)
        {
            PullPlan const& pull = plan.pull;
            PartyMemberFacts const* pullTank = world.FindMember(pull.tank);
            std::vector<ObjectGuid> lure;
            if (pull.phase == PullPhase::Settle && pull.losBreak == PullLosBreak::Active)
            {
                lure = pull.losCasters;
                d.lureCorner = prev.lureCorner;
                d.outliersWhy = "the tank is out of their sight, they walk to it";
            }
            else if (pull.phase == PullPhase::Settle && pull.losBreak == PullLosBreak::None && pullTank && pullTank->alive)
            {
                FightPlace::Lure const l = FightPlace::LureTargets(world, pull, *pullTank);
                lure = l.targets;
                d.lureCorner = l.corner;
                d.outliersWhy = l.corner ? "corner fight: the tank hides from the far ones"
                                         : "they stand back and cast: the tank hides from them";
            }
            // After the pull: the casters the caster lure works on (CasterLureAssigner, last refresh).
            else if (pull.phase == PullPhase::None)
            {
                for (LosLure const& l : plan.casterLure.lures)
                    if (l.state == LosLureState::Search || l.state == LosLureState::Active)
                        lure.insert(lure.end(), l.casters.begin(), l.casters.end());
                d.outliersWhy = "they stand back and cast: their victim steps out of their sight";
            }
            bool const anyOutlier = std::any_of(d.enemies.begin(), d.enemies.end(),
                [](DoctrineEnemy const& x) { return (x.stamps & STAMP_OUTLIER) != 0; });
            if (!lure.empty())
            {
                d.outliers = OutlierTactic::Lure;
                for (DoctrineEnemy& x : d.enemies)
                    if (std::find(lure.begin(), lure.end(), x.guid) != lure.end())
                        x.stamps |= STAMP_LURE;
            }
            else if (d.tactic == PileTactic::Area && anyOutlier)
            {
                d.outliers = OutlierTactic::KillFirst;
                d.outliersWhy = pull.losBreak == PullLosBreak::NoPoint
                    ? "no spot out of their sight: kill them first, the pile is the area spells'"
                    : "the pile is the area spells': kill them first";
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
            {
                noMez("area pile");
                // Not woken on purpose: the area spells may take it (a player's sheep is kept).
                if (e->control.spellId && e->control.breaksOnDamage)
                    if (PartyMemberFacts const* caster = world.FindMember(e->control.caster))
                        if (caster->isBot)
                            x.stamps |= STAMP_AREA_MAY_WAKE;
            }
            uint32 const othersFree = free - (IsFree(*e) ? 1 : 0);
            if (Tier(x.tier) == Tier::Fodder)
                noMez("fodder");
            // A doctrine hold (TacticCheck.h) stays while another enemy is still fought: holding
            // it is what brought the fight down to cleanup (log 11-15 183927: the hold dropped the
            // moment its shackle landed). The last one left is fought.
            if (d.phase == FightPhase::Cleanup && !(prev.HoldOn(x.guid) && othersFree > 0))
                noMez("cleanup");
            for (PartyMemberFacts const& m : world.party)
                if (!m.isBot && !m.isPet && m.alive && m.victim == e->guid)
                {
                    x.stamps |= STAMP_PLAYER_TARGET;
                    noMez("player hits it");
                }
            RoleStamps::Apply(world, *e, x);
            if ((Tier(x.tier) == Tier::Fodder || (x.stamps & STAMP_SWARM)) && !(x.stamps & STAMP_SELF_DESTRUCT))
                x.stamps |= STAMP_NO_HOLD;
            if (!(x.stamps & STAMP_NO_MEZ) && othersFree > 0 && HeldByBot(world, *e))
                x.stamps |= STAMP_COMMITTED;
        }

        AllyCharm::Apply(world, d);
        Stragglers::Apply(world, plan, d);
        // Before the check: an enemy on an off-tank is not a hold candidate.
        OffTank::Apply(world, plan, prev, d);
        TacticCheck::Apply(world, plan, prev, d, recheck);

        if (scope.On())
        {
            scope.Label("%s, %s, tank %s, outliers %s", Name(d.phase), Name(d.tactic), Name(d.tankJob), Name(d.outliers));
            std::string list;
            for (DoctrineEnemy const& x : d.enemies)
                list += Trace::Format("%s{\"guid\":\"%s\",\"group\":%d,\"stamps\":%u,\"engage\":%u,\"tier\":%u}",
                    list.empty() ? "" : ",", Trace::Guid(x.guid).c_str(), int(x.group), x.stamps, x.engage, x.tier);
            std::string groups;
            for (DoctrineGroup const& g : d.groups)
                groups += Trace::Format("%s\"%s\"", groups.empty() ? "" : ",", Battlefield::Describe(d, g).c_str());
            scope.Data(Trace::Format("{\"why\":\"%s\",\"freePile\":%u,\"areaDealers\":%u,\"outliers\":\"%s\",\"outliersWhy\":\"%s\",\"groups\":[%s],\"enemies\":[%s]}",
                d.why.c_str(), freePile, ledger.areaDealers, Name(d.outliers), d.outliersWhy.c_str(), groups.c_str(), list.c_str()));
        }
    }

    char const* TacticalDoctrine::Name(FightPhase phase)
    {
        switch (phase)
        {
            case FightPhase::None:      return "no fight";
            case FightPhase::Planned:   return "planned";
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

    char const* TacticalDoctrine::Name(TankJob job)
    {
        switch (job)
        {
            case TankJob::None:     return "no job";
            case TankJob::HoldPile: return "holds the pile, protects the area caster";
        }
        return "?";
    }

    char const* TacticalDoctrine::Name(OutlierTactic tactic)
    {
        switch (tactic)
        {
            case OutlierTactic::None:      return "none";
            case OutlierTactic::Lure:      return "lure by line of sight";
            case OutlierTactic::KillFirst: return "kill first";
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
        if (d.stamps & STAMP_LURE)          s += ", lured";
        if (Source(d.source) != Source::Static)
            s += std::string(", from ") + Tags::Name(Source(d.source));
        std::string const roles = Tags::RoleNames(d.roles);
        if (!roles.empty())
            s += ", roles: " + roles;
        if (d.stamps & STAMP_KILL_NOW)      s += ", kill it before it blows";
        if (d.stamps & STAMP_STAND_CLEAR)   s += ", stand clear of its blast";
        if (d.stamps & STAMP_SWARM)         s += ", swarm";
        if (d.stamps & STAMP_NO_TAUNT)      s += ", no taunt";
        if (d.killTier > 0)                 s += ", kill first (tier " + std::to_string(int(d.killTier)) + ")";
        if (d.killTier < 0)                 s += ", kill last (tier " + std::to_string(int(d.killTier)) + ")";
        if (d.stamps & STAMP_HOLD_FIRE)     s += ", hands off (reflects)";
        if (d.stamps & STAMP_FREE_ALLY)     s += ", kill it to free the ally";
        if (d.stamps & STAMP_STRAGGLER)     s += ", straggler (the tank's)";
        if (d.stamps & STAMP_DPS_ONLY)      s += ", left to the damage dealers";
        if (d.stamps & STAMP_MUST_CONTROL)  s += ", must be held (not bearable otherwise)";
        if (d.stamps & STAMP_NO_HOLD)       s += ", no tank hold";
        if (d.stamps & STAMP_AREA_MAY_WAKE) s += ", area spells may wake it";
        if (d.stamps & STAMP_OFF_TANK)      s += ", held by an off-tank";
        return s;
    }
}
