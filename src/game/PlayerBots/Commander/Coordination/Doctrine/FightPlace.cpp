#include "Commander/Coordination/Doctrine/FightPlace.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/Profiles/Tags.h"
#include "Commander/Perception/Spatial/PackResolver.h"
#include <algorithm>

namespace Commander
{
namespace FightPlace
{
    bool IsHard(FightLoad::Result const& r)
    {
        if (!FightLoad::Bearable(r) || r.lowestTankPct < CORNER_TANK_PCT)
            return true;
        return r.manaStart > 0.0f && r.manaLeft < r.manaStart * CORNER_MANA_LEFT_SHARE;
    }

    Choice Choose(Input const& in)
    {
        Choice c;
        c.hard = in.haveEstimate && IsHard(in.estimate);
        if (in.wanted == PullPlace::Corner)
        {
            c.place = Kind::Corner;
            c.why = "the leader chose corner fights";
        }
        else if (in.distant && !in.fightThere && in.spotRisk < in.packRisk)
        {
            c.place = Kind::BringBack;
            c.why = "add risk at the pack, the leader's spot is safer";
        }
        else if (c.hard)
        {
            c.place = Kind::Corner;
            c.why = "the fight looks hard: " + FightLoad::Describe(in.estimate);
        }
        else
        {
            c.place = Kind::Here;
            c.why = !in.distant ? "they are next to the leader"
                  : in.fightThere ? "the party can take an add there"
                  : "the leader's spot is no safer";
        }
        return c;
    }

    char const* Name(Kind place)
    {
        switch (place)
        {
            case Kind::Here:      return "here";
            case Kind::BringBack: return "bring back";
            case Kind::Corner:    return "corner";
        }
        return "?";
    }

    Lure LureTargets(WorldModel const& world, PullPlan const& pull, PartyMemberFacts const& tank)
    {
        struct Candidate
        {
            ObjectGuid guid;
            bool standsBack;    // casting from where it stands now
            bool caster;        // has a main ranged spell
            float distance;
        };
        std::vector<Candidate> found;
        for (EnemyFacts const& e : world.enemies)
        {
            if (e.onAllyOnly || !e.acting || !e.health || e.crowdControlled || e.victim != pull.tank || !pull.Expects(e))
                continue;
            float const d = PackResolver::Distance(e.pos, tank.pos);
            if (d <= LOS_CASTER_MIN_DISTANCE)
                continue;
            bool const caster = Tags::EngageOfEntry(e.entry) == Engage::StandBack;
            found.push_back({ e.guid, !e.meleeEnabled || (caster && !e.moving), caster, d });
        }

        Lure out;
        for (Candidate const& c : found)
            if (c.standsBack && out.targets.size() < LOS_MAX_CASTERS)
                out.targets.push_back(c.guid);
        if (!out.targets.empty() || !pull.corner)
            return out;

        std::sort(found.begin(), found.end(), [](Candidate const& a, Candidate const& b)
        {
            if (a.caster != b.caster)
                return a.caster;
            return a.distance > b.distance;
        });
        for (Candidate const& c : found)
        {
            out.targets.push_back(c.guid);
            if (out.targets.size() >= LOS_MAX_CASTERS)
                break;
        }
        out.corner = !out.targets.empty();
        return out;
    }
}
}
