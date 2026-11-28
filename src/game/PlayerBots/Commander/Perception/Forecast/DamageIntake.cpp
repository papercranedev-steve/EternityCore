#include "Commander/Perception/Forecast/DamageIntake.h"
#include "Commander/CommanderMgr.h"
#include "Unit.h"
#include "Timer.h"
#include <algorithm>
#include <deque>
#include <mutex>
#include <unordered_map>

namespace Commander
{
namespace DamageIntake
{
    namespace
    {
        struct Hit
        {
            uint32 timeMs;
            uint32 amount;
        };

        // Victims without a hit for KEEP_MS are removed every PRUNE_EVERY hits.
        uint32 const PRUNE_EVERY = 1000;

        std::mutex s_lock;
        std::unordered_map<uint64, std::deque<Hit>> s_hits;
        uint32 s_sincePrune = 0;

        void DropOld(std::deque<Hit>& hits, uint32 nowMs)
        {
            while (!hits.empty() && WorldTimer::getMSTimeDiff(hits.front().timeMs, nowMs) > KEEP_MS)
                hits.pop_front();
        }
    }

    void OnDamage(Unit const* victim, uint32 damage)
    {
        if (!damage || !victim || !sCommanderMgr.IsEnabled() || !victim->GetCharmerOrOwnerPlayerOrPlayerItself())
            return;
        uint32 const nowMs = WorldTimer::getMSTime();
        uint64 const key = victim->GetObjectGuid().GetRawValue();

        std::lock_guard<std::mutex> guard(s_lock);
        std::deque<Hit>& hits = s_hits[key];
        DropOld(hits, nowMs);
        hits.push_back({ nowMs, damage });

        if (++s_sincePrune >= PRUNE_EVERY)
        {
            s_sincePrune = 0;
            for (auto itr = s_hits.begin(); itr != s_hits.end();)
            {
                DropOld(itr->second, nowMs);
                if (itr->second.empty())
                    itr = s_hits.erase(itr);
                else
                    ++itr;
            }
        }
    }

    Stats Query(ObjectGuid victim, uint32 nowMs)
    {
        Stats s;
        std::lock_guard<std::mutex> guard(s_lock);
        auto const found = s_hits.find(victim.GetRawValue());
        if (found == s_hits.end())
            return s;
        std::deque<Hit> const& hits = found->second;

        float recent = 0.0f;
        float window = 0.0f;
        size_t first = 0;
        for (size_t i = 0; i < hits.size(); ++i)
        {
            uint32 const age = WorldTimer::getMSTimeDiff(hits[i].timeMs, nowMs);
            if (age > KEEP_MS)
            {
                first = i + 1;
                continue;
            }
            if (age <= RATE_WINDOW_MS)
            {
                recent += float(hits[i].amount);
                s.hitsPerSec += 1.0f;
            }

            // Hits within BURST_WINDOW_MS ending at hit i.
            window += float(hits[i].amount);
            while (first < i && WorldTimer::getMSTimeDiff(hits[first].timeMs, hits[i].timeMs) > BURST_WINDOW_MS)
                window -= float(hits[first++].amount);
            s.burst = std::max(s.burst, window);
        }
        s.dps = recent * 1000.0f / float(RATE_WINDOW_MS);
        s.hitsPerSec = s.hitsPerSec * 1000.0f / float(RATE_WINDOW_MS);
        return s;
    }
}
}
