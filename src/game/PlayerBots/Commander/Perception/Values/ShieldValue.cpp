#include "Commander/Perception/Values/ShieldValue.h"
#include "Commander/Perception/Forecast/CastPushback.h"
#include "Commander/Perception/Values/ValueModel.h"
#include "Commander/Perception/WorldModel.h"
#include <algorithm>

namespace Commander
{
namespace ShieldValue
{
    float TypicalCastSec(PartyMemberFacts const& member)
    {
        if (member.HealsTeam())
        {
            float longest = 0.0f;
            for (HealRate const& r : member.healRates)
                longest = std::max(longest, r.castSec);
            if (longest > 0.0f)
                return longest;
        }
        return float(member.fillerCastMs) / 1000.0f;
    }

    Result Of(WorldModel const& world, PartyMemberFacts const& member, float absorb)
    {
        Result out;
        if (absorb <= 0.0f)
            return out;
        std::vector<CastPushback::Hit> const hits = CastPushback::Hits(world, member, HORIZON_SEC);
        float total = 0.0f;
        for (CastPushback::Hit const& h : hits)
            total += h.damage;
        out.absorbed = std::min(absorb, std::max(0.0f, total - member.absorb));
        float const castSec = TypicalCastSec(member);
        if (castSec > 0.0f)
            out.pushbackSec = std::max(0.0f, CastPushback::LostSec(hits, castSec, HORIZON_SEC, member.absorb) -
                                             CastPushback::LostSec(hits, castSec, HORIZON_SEC, member.absorb + absorb));
        out.benefit = Value::Points(world, out.absorbed) + Value::LostAction(world, member, out.pushbackSec);
        return out;
    }
}
}
