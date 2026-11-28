#include "Commander/Perception/Forecast/Runners.h"
#include "Commander/Perception/Values/AreaValue.h"
#include <algorithm>
#include <cmath>

namespace Commander
{
namespace Runners
{
    namespace
    {
        float Distance2d(Position3 const& a, Position3 const& b)
        {
            float const dx = a.x - b.x, dy = a.y - b.y;
            return std::sqrt(dx * dx + dy * dy);
        }
    }

    std::vector<Runner> Of(WorldModel const& world, ObjectGuid member, Position3 const& at)
    {
        std::vector<Runner> out;
        for (EnemyFacts const& e : world.enemies)
        {
            if (!e.acting || e.crowdControlled || e.rooted || !e.meleeEnabled || !e.health || e.victim != member)
                continue;
            out.push_back({ &e, std::max(0.0f, Distance2d(e.pos, at) - e.boundingRadius) });
        }
        std::sort(out.begin(), out.end(), [](Runner const& a, Runner const& b) { return a.gapYd < b.gapYd; });
        return out;
    }

    float ArriveSec(EnemyFacts const& e, Position3 const& at, AreaSpellFacts const* spell, Position3 const& centre)
    {
        return ReachSec(e, at, e.boundingRadius + AreaValue::MELEE_REACH, spell, centre);
    }

    float ReachSec(EnemyFacts const& e, Position3 const& at, float reach, AreaSpellFacts const* spell, Position3 const& centre)
    {
        reach = std::max(0.0f, reach);
        float const length = Distance2d(e.pos, at);
        float const walk = std::max(0.0f, length - reach);
        float const speed = std::max(0.1f, e.speed);
        if (!spell || spell->slowPct <= 0.0f || e.slowed || spell->periodSec <= 0.0f)
            return walk / speed;
        // Full speed until the first tick, then slowed inside the area.
        float const first = speed * spell->periodSec;
        if (walk <= first || length <= 0.01f)
            return walk / speed;
        Position3 p = e.pos;
        p.x += (at.x - e.pos.x) * first / length;
        p.y += (at.y - e.pos.y) * first / length;
        return spell->periodSec + AreaValue::WalkSecUnder(p, speed, spell->slowPct, centre, spell->radius, at, reach);
    }

    Position3 PosAfter(WorldModel const& world, EnemyFacts const& e, float sec, ObjectGuid mover,
                       Position3 const& moverAt)
    {
        if (sec <= 0.0f || !e.acting || e.crowdControlled || e.rooted || !e.meleeEnabled || !e.health ||
            e.victim.IsEmpty() || e.speed <= 0.0f)
            return e.pos;
        Position3 to;
        if (!mover.IsEmpty() && e.victim == mover)
            to = moverAt;
        else if (PartyMemberFacts const* m = world.FindMember(e.victim))
            to = m->pos;
        else if (AllyFacts const* a = world.FindAlly(e.victim))
            to = a->pos;
        else
            return e.pos;
        float const runSec = sec - (e.castingSpellId ? float(e.castRemainingMs) / 1000.0f : 0.0f);
        float const length = Distance2d(e.pos, to);
        float const walk = std::min(std::max(0.0f, length - e.boundingRadius - CHASE_GAP_YD),
                                    std::max(0.0f, runSec) * e.speed);
        if (walk <= 0.0f || length <= 0.01f)
            return e.pos;
        Position3 p = e.pos;
        p.x += (to.x - e.pos.x) * walk / length;
        p.y += (to.y - e.pos.y) * walk / length;
        p.z += (to.z - e.pos.z) * walk / length;
        return p;
    }
}
}
