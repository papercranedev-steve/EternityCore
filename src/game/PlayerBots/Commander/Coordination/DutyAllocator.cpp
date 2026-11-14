#include "Commander/Coordination/DutyAllocator.h"
#include <algorithm>
#include <map>
#include <utility>

namespace Commander
{
namespace DutyAllocator
{
    char const* KindName(DutyKind kind)
    {
        switch (kind)
        {
            case DutyKind::Interrupt: return "interrupt";
            case DutyKind::Intercept: return "intercept";
            case DutyKind::Dispel: return "dispel";
            case DutyKind::CrowdControl: return "control";
            case DutyKind::Rescue: return "rescue";
            case DutyKind::Save: return "save";
            case DutyKind::FleeStop: return "flee stop";
            case DutyKind::Boost: return "boost";
        }
        return "?";
    }

    std::vector<DutyResult> Allocate(std::vector<DutyNeed> needs)
    {
        std::stable_sort(needs.begin(), needs.end(), [](DutyNeed const& a, DutyNeed const& b)
        {
            if (a.now != b.now)
                return a.now;
            if (a.points != b.points)
                return a.points > b.points;
            return a.tieMs < b.tieMs;
        });

        struct Claim
        {
            DutyKind kind;
            ObjectGuid target;
        };
        std::map<ObjectGuid, Claim> primaries;     // bot -> the primary duty it holds

        std::vector<DutyKey> handled;     // marked by answered needs

        std::vector<DutyResult> results;
        for (DutyNeed& need : needs)
        {
            if (std::any_of(need.blockedBy.begin(), need.blockedBy.end(), [&handled](DutyKey const& k)
                    { return std::find(handled.begin(), handled.end(), k) != handled.end(); }))
                continue;

            std::stable_sort(need.candidates.begin(), need.candidates.end(), [](DutyCandidate const& a, DutyCandidate const& b)
            {
                return a.rank > b.rank;
            });

            DutyResult r;
            r.kind = need.kind;
            r.target = need.target;
            // Fix: was not copied, so every dispel duty had debuff 0 and DispelBehavior's
            // HasAura(0) check dropped it (dispels never cast since 2026-11-03).
            r.auraSpellId = need.auraSpellId;
            r.now = need.now;
            r.points = need.points;
            r.detail = need.detail;
            r.reason = need.reason;

            for (DutyCandidate const& c : need.candidates)
            {
                auto const held = primaries.find(c.bot);
                if (c.canBePrimary && held == primaries.end())
                {
                    r.primary = c.bot;
                    r.primarySpellId = c.spellId;
                    primaries[c.bot] = { need.kind, need.target };
                    break;
                }
                DutyPass pass;
                pass.bot = c.bot;
                if (held != primaries.end())
                {
                    pass.reason = DutyPassReason::Busy;
                    pass.busyKind = held->second.kind;
                    pass.busyTarget = held->second.target;
                }
                else
                    pass.reason = DutyPassReason::NotPrimary;
                r.passed.push_back(pass);
            }

            // One entry per bot (a bot offers one candidate per ability), not the chosen one.
            std::vector<DutyPass> passed;
            for (DutyPass const& p : r.passed)
                if (p.bot != r.primary && std::none_of(passed.begin(), passed.end(),
                        [&p](DutyPass const& q) { return q.bot == p.bot; }))
                    passed.push_back(p);
            r.passed = std::move(passed);

            if (need.wantsBackup)
            {
                for (bool allowBusy : { false, true })
                {
                    for (DutyCandidate const& c : need.candidates)
                    {
                        if (c.bot == r.primary || (!allowBusy && primaries.count(c.bot)))
                            continue;
                        r.backup = c.bot;
                        r.backupSpellId = c.spellId;
                        break;
                    }
                    if (!r.backup.IsEmpty())
                        break;
                }
            }

            if (!r.primary.IsEmpty())
                handled.insert(handled.end(), need.marks.begin(), need.marks.end());
            if (!r.primary.IsEmpty() || !r.backup.IsEmpty())
                results.push_back(std::move(r));
        }
        return results;
    }
}
}
