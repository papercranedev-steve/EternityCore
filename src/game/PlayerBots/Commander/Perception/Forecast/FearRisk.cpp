#include "Commander/Perception/Forecast/FearRisk.h"
#include "Commander/Perception/Spatial/PackResolver.h"
#include <algorithm>
#include <cmath>

namespace Commander
{
namespace FearRisk
{
    namespace
    {
        uint32 const SAMPLE_RINGS = 4;
        uint32 const SAMPLE_ANGLES = 16;

        bool MayStartFight(PackCreature const& c)
        {
            return c.aggressive && !c.noAggro && !c.tappedByOther;
        }

        float Distance2d(Position3 const& a, float x, float y)
        {
            return std::sqrt((a.x - x) * (a.x - x) + (a.y - y) * (a.y - y));
        }

        bool IsMeleeChaser(PartyMemberFacts const& m)
        {
            return m.alive && (m.isPet || m.role == ROLE_TANK || m.role == ROLE_MELEE_DPS);
        }

        // Share of the points around `caster` a chaser following the feared creature would
        // stand in that are inside a member's aggro reach.
        float CoveredShare(PackFacts const& pack, Position3 const& caster)
        {
            uint32 covered = 0;
            uint32 total = 0;
            for (uint32 r = 1; r <= SAMPLE_RINGS; ++r)
            {
                float const dist = MAX_RUN_YD * float(r) / float(SAMPLE_RINGS);
                for (uint32 a = 0; a < SAMPLE_ANGLES; ++a)
                {
                    float const angle = 2.0f * float(M_PI) * float(a) / float(SAMPLE_ANGLES);
                    float const x = caster.x + dist * std::cos(angle);
                    float const y = caster.y + dist * std::sin(angle);
                    ++total;
                    for (PackCreature const& c : pack.members)
                    {
                        if (MayStartFight(c) && c.reach > 0.0f && Distance2d(c.pos, x, y) <= c.reach + CHASE_MARGIN_YD)
                        {
                            ++covered;
                            break;
                        }
                    }
                }
            }
            return total ? float(covered) / float(total) : 0.0f;
        }
    }

    std::vector<PackRisk> Of(WorldModel const& world, Position3 const& caster, float radius, float durationSec,
                             std::vector<EnemyFacts const*> const& feared)
    {
        std::vector<PackRisk> out;
        bool chaser = false;
        for (PartyMemberFacts const& m : world.party)
            for (EnemyFacts const* e : feared)
                if (e && IsMeleeChaser(m) && m.victim == e->guid)
                    chaser = true;
        float const legs = std::max(1.0f, durationSec / LEG_SEC);

        for (PackFacts const& pack : world.packs)
        {
            PackRisk risk;
            risk.pack = &pack;
            if (radius > 0.0f)
                for (PackCreature const& c : pack.members)
                    if (!c.noAggro && PackResolver::Distance(c.pos, caster) <= radius)
                        risk.certain = true;
            if (!risk.certain && !std::any_of(pack.members.begin(), pack.members.end(), MayStartFight))
                continue;
            if (risk.certain)
                risk.chance = 1.0f;
            else
            {
                float const f = CoveredShare(pack, caster);
                risk.chance = (1.0f - std::pow(1.0f - f, legs)) * (chaser ? 1.0f : NO_CHASER_FACTOR);
            }
            if (risk.chance >= MIN_CHANCE)
                out.push_back(risk);
        }
        std::sort(out.begin(), out.end(), [](PackRisk const& a, PackRisk const& b) { return a.chance > b.chance; });
        return out;
    }
}
}
