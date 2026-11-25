#include "Commander/Coordination/Doctrine/Battlefield.h"
#include "Commander/Coordination/Doctrine/TacticalDoctrine.h"
#include "Commander/Perception/Spatial/PackResolver.h"
#include "Commander/Perception/Spatial/PatrolZone.h"
#include "Timer.h"
#include <algorithm>
#include <cstdio>

namespace Commander
{
namespace Battlefield
{
    namespace
    {
        float const CLUSTER_YD = TacticalDoctrine::CLUSTER_YD;

        DoctrineGroup& Make(std::vector<DoctrineGroup>& groups, GroupKind kind, bool planned, uint32 nowMs)
        {
            groups.emplace_back();
            DoctrineGroup& g = groups.back();
            g.kind = kind;
            g.planned = planned;
            g.sinceMs = nowMs;
            return g;
        }

        // The first group of `kind` (made when there is none).
        int Shared(std::vector<DoctrineGroup>& groups, GroupKind kind, bool planned, uint32 nowMs)
        {
            for (size_t i = 0; i < groups.size(); ++i)
                if (groups[i].kind == kind)
                    return int(i);
            Make(groups, kind, planned, nowMs);
            return int(groups.size()) - 1;
        }

        bool AtTank(EnemyFacts const& e, PartyMemberFacts const& tank)
        {
            return PackResolver::Distance(tank.pos, e.pos) - e.boundingRadius <= CLUSTER_YD;
        }

        // The add batch `e` belongs to (header: adds), its group of e's kind in it.
        int AddBatchGroup(WorldModel const& world, EnemyFacts const& e, uint32 nowMs, std::vector<DoctrineGroup>& groups)
        {
            uint32 batch = 0;
            uint32 last = 0;
            for (DoctrineGroup const& g : groups)
            {
                last = std::max(last, g.addBatch);
                if (batch || !g.addBatch || WorldTimer::getMSTimeDiff(g.sinceMs, nowMs) > ADD_BATCH_MS)
                    continue;
                for (ObjectGuid m : g.members)
                    if (EnemyFacts const* o = world.FindEnemy(m))
                        if (PackResolver::Distance(o->pos, e.pos) <= ADD_LINK_YD)
                        {
                            batch = g.addBatch;
                            break;
                        }
            }
            if (!batch)
                batch = last + 1;
            GroupKind const kind = Tags::EngageOf(e) == Engage::Melee ? GroupKind::MeleeStraggler : GroupKind::RangedStraggler;
            for (size_t i = 0; i < groups.size(); ++i)
                if (groups[i].addBatch == batch && groups[i].kind == kind)
                    return int(i);
            Make(groups, kind, false, nowMs).addBatch = batch;
            return int(groups.size()) - 1;
        }
    }

    std::vector<DoctrineGroup> FromPull(WorldModel const& world, std::vector<Unit> const& expected,
                                        ObjectGuid target, uint32 nowMs, std::vector<DoctrineGroup> const* prev)
    {
        std::vector<DoctrineGroup> groups;
        if (expected.empty())
            return groups;
        // The last split's pile (PILE_KEEP_YD).
        std::vector<bool> wasInPile(expected.size(), false);
        bool anyWasInPile = false;
        if (prev)
            for (DoctrineGroup const& g : *prev)
                if (g.kind == GroupKind::Pile)
                    for (size_t i = 0; i < expected.size(); ++i)
                        if (std::find(g.members.begin(), g.members.end(), expected[i].guid) != g.members.end())
                            anyWasInPile = wasInPile[i] = true;
        std::vector<uint32> eliteHealth;
        for (Unit const& u : expected)
            if (u.elite && u.maxHealth)
                eliteHealth.push_back(u.maxHealth);
        float const median = Tags::MedianHealth(std::move(eliteHealth));

        std::vector<bool> melee(expected.size());
        Unit const* targetUnit = nullptr;
        for (size_t i = 0; i < expected.size(); ++i)
        {
            melee[i] = Tags::EngageOfEntry(expected[i].entry) == Engage::Melee;
            if (expected[i].guid == target)
                targetUnit = &expected[i];
        }
        if (!targetUnit)
            targetUnit = &expected.front();

        // The pile's seed: the target, or the melee one nearest to it (of the last pile while
        // it had one: the nearest one changes as they wander).
        int seed = -1;
        bool const targetMelee = melee[size_t(targetUnit - &expected.front())];
        for (size_t i = 0; i < expected.size(); ++i)
        {
            if (!melee[i] || (!targetMelee && anyWasInPile && !wasInPile[i]))
                continue;
            if (&expected[i] == targetUnit)
            {
                seed = int(i);
                break;
            }
            if (seed < 0 || PackResolver::Distance(expected[i].pos, targetUnit->pos) <
                            PackResolver::Distance(expected[seed].pos, targetUnit->pos))
                seed = int(i);
        }

        // The pile: melee ones linked to the seed within CLUSTER_YD of each other.
        std::vector<bool> inPile(expected.size(), false);
        if (seed >= 0)
        {
            inPile[seed] = true;
            std::vector<size_t> open = { size_t(seed) };
            while (!open.empty())
            {
                size_t const at = open.back();
                open.pop_back();
                for (size_t i = 0; i < expected.size(); ++i)
                    if (melee[i] && !inPile[i] && PackResolver::Distance(expected[i].pos, expected[at].pos) <=
                        (wasInPile[i] && wasInPile[at] ? PILE_KEEP_YD : CLUSTER_YD))
                    {
                        inPile[i] = true;
                        open.push_back(i);
                    }
            }
            DoctrineGroup& pile = Make(groups, GroupKind::Pile, true, nowMs);
            for (size_t i = 0; i < expected.size(); ++i)
                if (inPile[i])
                {
                    pile.members.push_back(expected[i].guid);
                    if (pile.heavy.IsEmpty() &&
                        Tags::TierOf(world, expected[i].elite, expected[i].maxHealth, median) == Tier::Heavy)
                        pile.heavy = expected[i].guid;
                }
        }
        // (melee stragglers first, always: the order is part of the split TacticalDoctrine compares)
        for (bool const meleePass : { true, false })
            for (size_t i = 0; i < expected.size(); ++i)
            {
                if (inPile[i] || melee[i] != meleePass)
                    continue;
                int const g = Shared(groups, melee[i] ? GroupKind::MeleeStraggler : GroupKind::RangedStraggler, true, nowMs);
                groups[g].members.push_back(expected[i].guid);
            }
        return groups;
    }

    void AddPatrols(WorldModel const& world, Position3 const& spot, float withinSec,
                    std::vector<ObjectGuid> const& expected, uint32 nowMs, std::vector<DoctrineGroup>& groups)
    {
        for (PatrolFacts const& p : world.patrols)
        {
            if (std::find(expected.begin(), expected.end(), p.guid) != expected.end() || Find(groups, p.guid) >= 0)
                continue;
            float const arrive = PatrolZone::ArriveSec(world, p, spot);
            if (arrive < 0.0f || arrive >= withinSec)
                continue;
            DoctrineGroup& g = Make(groups, GroupKind::Patrol, true, nowMs);
            g.members.push_back(p.guid);
            g.arriveSec = arrive;
        }
    }

    int Place(WorldModel const& world, EnemyFacts const& e, PartyMemberFacts const& tank,
              bool byPlaceOnly, uint32 nowMs, std::vector<DoctrineGroup>& groups)
    {
        int g = -1;
        if (!byPlaceOnly)
        {
            switch (Tags::SourceOf(e))
            {
                case Source::Patrol:
                    Make(groups, GroupKind::Patrol, false, nowMs);
                    g = int(groups.size()) - 1;
                    break;
                case Source::DeathSpawn:
                case Source::Called:
                case Source::Summoned:
                case Source::Trap:
                    g = Shared(groups, GroupKind::Reinforcement, false, nowMs);
                    break;
                default:
                    if (!AtTank(e, tank))
                        g = AddBatchGroup(world, e, nowMs, groups);
                    break;
            }
        }
        if (g < 0)
        {
            GroupKind const kind = AtTank(e, tank) ? GroupKind::Pile
                                 : Tags::EngageOf(e) == Engage::Melee ? GroupKind::MeleeStraggler
                                 : GroupKind::RangedStraggler;
            g = Shared(groups, kind, false, nowMs);
        }
        groups[g].members.push_back(e.guid);
        return g;
    }

    bool UpdateJoined(EnemyFacts const& e, PartyMemberFacts const& tank, DoctrineGroup& g)
    {
        if (g.kind == GroupKind::Pile || g.Joined(e.guid))
            return true;
        if (g.holdOut || !AtTank(e, tank))
            return false;
        g.joined.push_back(e.guid);
        return true;
    }

    int Find(std::vector<DoctrineGroup> const& groups, ObjectGuid guid)
    {
        for (size_t i = 0; i < groups.size(); ++i)
            if (groups[i].Has(guid))
                return int(i);
        return -1;
    }

    uint32 StampOf(GroupKind kind, bool joined, bool holdOut, Engage engage, Tier tier)
    {
        if (holdOut)
            return STAMP_SPECIAL;
        if (kind == GroupKind::Pile || joined)
            return STAMP_IN_CLUSTER;
        if (tier == Tier::Heavy)
            return STAMP_SPECIAL;
        return engage == Engage::Melee ? STAMP_IN_CLUSTER : STAMP_OUTLIER;
    }

    std::vector<std::pair<ObjectGuid, char const*>> PullNoMez(WorldModel const& world,
        std::vector<Unit> const& expected, ObjectGuid target)
    {
        std::vector<DoctrineGroup> const groups = FromPull(world, expected, target, 0);
        std::vector<uint32> eliteHealth;
        for (Unit const& u : expected)
            if (u.elite && u.maxHealth)
                eliteHealth.push_back(u.maxHealth);
        float const median = Tags::MedianHealth(std::move(eliteHealth));

        std::vector<std::pair<ObjectGuid, char const*>> out;
        std::vector<ObjectGuid> pile;
        for (Unit const& u : expected)
        {
            Tier const tier = Tags::TierOf(world, u.elite, u.maxHealth, median);
            int const g = Find(groups, u.guid);
            if (tier == Tier::Fodder)
                out.push_back({ u.guid, "fodder" });
            if (g >= 0 && StampOf(groups[g].kind, false, false, Tags::EngageOfEntry(u.entry), tier) == STAMP_IN_CLUSTER)
                pile.push_back(u.guid);
        }
        if (Tags::LedgerOf(world).areaDealers > 0 && pile.size() >= TacticalDoctrine::AREA_MIN)
            for (ObjectGuid g : pile)
                out.push_back({ g, "area pile" });
        return out;
    }

    char const* Name(GroupKind kind)
    {
        switch (kind)
        {
            case GroupKind::Pile:            return "pile";
            case GroupKind::MeleeStraggler:  return "melee stragglers";
            case GroupKind::RangedStraggler: return "standing back";
            case GroupKind::Patrol:          return "patrol";
            case GroupKind::Reinforcement:   return "reinforcements";
        }
        return "?";
    }

    std::string Describe(DoctrinePlan const& d, DoctrineGroup const& g)
    {
        std::string s = Name(g.kind);
        if (g.kind == GroupKind::Pile && !g.heavy.IsEmpty())
            s += g.members.size() > 1 ? " (boss + adds)" : " (one heavy)";
        char buf[64];
        if (g.addBatch)
        {
            snprintf(buf, sizeof(buf), " (add %u)", g.addBatch);
            s += buf;
        }
        snprintf(buf, sizeof(buf), ", %u", uint32(g.members.size()));
        s += buf;
        if (!g.planned && !g.addBatch)
            s += ", came in the fight";
        std::string tactic;
        switch (g.kind)
        {
            case GroupKind::Pile:
                tactic = d.tactic != PileTactic::None ? TacticalDoctrine::Name(d.tactic) : "";
                break;
            case GroupKind::MeleeStraggler:
                tactic = "they walk to the pile";
                break;
            case GroupKind::RangedStraggler:
                tactic = d.outliers != OutlierTactic::None ? TacticalDoctrine::Name(d.outliers) : "";
                break;
            case GroupKind::Patrol:
            case GroupKind::Reinforcement:
                if (g.arriveSec >= 0.0f && d.phase == FightPhase::Planned)
                {
                    snprintf(buf, sizeof(buf), "comes in about %.0f s, ", g.arriveSec);
                    tactic = buf;
                }
                tactic += g.holdOut ? "held outside the pile" : "joins the fight";
                break;
        }
        if (!g.joined.empty())
        {
            snprintf(buf, sizeof(buf), "%s%u joined the pile", tactic.empty() ? "" : "; ", uint32(g.joined.size()));
            tactic += buf;
        }
        if (!tactic.empty())
            s += ": " + tactic;
        return s;
    }
}
}
