#include "Commander/Perception/Forecast/DamageDealt.h"
#include "Commander/CommanderMgr.h"
#include "Unit.h"
#include "Timer.h"
#include <algorithm>
#include <deque>
#include <mutex>
#include <unordered_map>

namespace Commander
{
namespace DamageDealt
{
    namespace
    {
        struct Hit
        {
            uint32 timeMs;
            uint64 attacker;
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

    void OnDamage(Unit const* attacker, Unit const* victim, uint32 damage, bool periodic)
    {
        if (!damage || periodic || !attacker || !victim || attacker == victim || !victim->IsCreature() ||
            !sCommanderMgr.IsEnabled() || victim->IsCharmerOrOwnerPlayerOrPlayerItself() ||
            !attacker->IsCharmerOrOwnerPlayerOrPlayerItself())
            return;
        uint32 const nowMs = WorldTimer::getMSTime();
        uint64 const key = victim->GetObjectGuid().GetRawValue();

        std::lock_guard<std::mutex> guard(s_lock);
        std::deque<Hit>& hits = s_hits[key];
        DropOld(hits, nowMs);
        hits.push_back({ nowMs, attacker->GetObjectGuid().GetRawValue(), damage });

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

    std::vector<Rate> Query(ObjectGuid victim, uint32 nowMs)
    {
        std::vector<Rate> out;
        std::lock_guard<std::mutex> guard(s_lock);
        auto const found = s_hits.find(victim.GetRawValue());
        if (found == s_hits.end())
            return out;
        for (Hit const& h : found->second)
        {
            if (WorldTimer::getMSTimeDiff(h.timeMs, nowMs) > RATE_WINDOW_MS)
                continue;
            ObjectGuid const attacker(h.attacker);
            auto itr = std::find_if(out.begin(), out.end(), [&attacker](Rate const& r) { return r.attacker == attacker; });
            if (itr == out.end())
            {
                out.push_back({ attacker, 0.0f });
                itr = out.end() - 1;
            }
            itr->dps += float(h.amount);
        }
        for (Rate& r : out)
            r.dps = r.dps * 1000.0f / float(RATE_WINDOW_MS);
        return out;
    }
}
}
