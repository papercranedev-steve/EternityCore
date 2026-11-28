#include "Commander/Perception/Forecast/CastPushback.h"
#include "Commander/Perception/Forecast/IncomingTimeline.h"
#include "Commander/Perception/WorldModel.h"
#include <algorithm>

namespace Commander
{
namespace CastPushback
{
    std::vector<Hit> Hits(WorldModel const& world, PartyMemberFacts const& member, float horizonSec)
    {
        std::vector<Hit> out;
        IncomingTimeline::Timeline const timeline = IncomingTimeline::Of(world, member);
        for (IncomingTimeline::Event const& e : timeline.events)
            if (e.expected > 0.0f && !e.periodic && e.atSec >= 0.0f && e.atSec < horizonSec)
                out.push_back({ e.atSec, e.expected });
        // The measured damage the model does not explain, as evenly spaced hits (header).
        float const residual = timeline.residualDps;
        if (residual > 0.0f && member.takenDps > 0.0f && member.takenHitsPerSec > 0.0f)
        {
            float const rate = member.takenHitsPerSec * std::min(1.0f, residual / member.takenDps);
            if (rate > 0.0f)
            {
                float const size = residual / rate;
                for (float t = 0.5f / rate; t < horizonSec; t += 1.0f / rate)
                    out.push_back({ t, size });
            }
        }
        std::sort(out.begin(), out.end(), [](Hit const& a, Hit const& b) { return a.atSec < b.atSec; });
        return out;
    }

    float DelaySec(std::vector<Hit> const& hits, float startSec, float castSec, float& absorb)
    {
        if (castSec <= 0.0f)
            return 0.0f;
        float end = startSec + castSec;
        float delayed = 0.0f;
        uint32 count = 0;
        for (Hit const& hit : hits)
        {
            if (hit.atSec < startSec)
                continue;
            if (hit.atSec >= end)
                break;
            if (absorb >= hit.damage)
            {
                absorb -= hit.damage;
                continue;
            }
            absorb = 0.0f;
            float delay = count < 5 ? 1.0f - 0.2f * float(count) : 0.2f;
            ++count;
            // Never back past the start: at most the progress made so far.
            float const progress = std::max(0.0f, hit.atSec - startSec - delayed);
            delay = std::min(delay, progress);
            delayed += delay;
            end += delay;
        }
        return delayed;
    }

    float LostSec(std::vector<Hit> const& hits, float castSec, float horizonSec, float absorb)
    {
        if (castSec <= 0.0f)
            return 0.0f;
        float lost = 0.0f;
        for (float t = 0.0f; t < horizonSec;)
        {
            float const d = DelaySec(hits, t, castSec, absorb);
            lost += d;
            t += castSec + d;
        }
        return lost;
    }
}
}
