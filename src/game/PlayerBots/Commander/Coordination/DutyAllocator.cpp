#include "Commander/Coordination/DutyAllocator.h"
#include <algorithm>
#include <functional>
#include <limits>
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
            case DutyKind::Resurrect: return "resurrect";
        }
        return "?";
    }

    std::string DescribePass(DutyPass const& pass, std::string const& busyName)
    {
        switch (pass.reason)
        {
            case DutyPassReason::Busy:
                return std::string("primary ") + KindName(pass.busyKind) + " on " + busyName;
            case DutyPassReason::NotPrimary:
                return "backup only (no sight, too far or not in front)";
            case DutyPassReason::LessWorth:
                return "free, the allocation is worth more without it";
        }
        return "?";
    }

    namespace
    {
        float const EPSILON = 0.0001f;
        int const UNANSWERED = -1;
        int const BLOCKED = -2;

        // Total value of the answered needs: those acting now, then the others (lexicographic).
        struct Score
        {
            float now = 0.0f;
            float later = 0.0f;
        };

        bool Beats(Score const& a, Score const& b)
        {
            if (a.now > b.now + EPSILON)
                return true;
            if (a.now < b.now - EPSILON)
                return false;
            return a.later > b.later + EPSILON;
        }

        float ValueOf(DutyNeed const& need, DutyCandidate const& c)
        {
            return c.valued ? c.value : need.points;
        }

        bool IsBlocked(DutyNeed const& need, std::vector<DutyKey> const& handled)
        {
            return std::any_of(need.blockedBy.begin(), need.blockedBy.end(), [&handled](DutyKey const& k)
                { return std::find(handled.begin(), handled.end(), k) != handled.end(); });
        }

        // Branch and bound over the needs in order (header). choice[i] = index into
        // needs[i].candidates, UNANSWERED or BLOCKED.
        class Search
        {
        public:
            Search(std::vector<DutyNeed> const& needs, std::vector<DutyReserve> const& reserves)
                : m_needs(needs), m_reserves(reserves), m_choice(needs.size(), UNANSWERED)
            {
                // The best a need can add (bound): its most valuable candidate fit to be primary.
                m_best.reserve(needs.size());
                for (DutyNeed const& n : needs)
                {
                    float best = 0.0f;
                    for (DutyCandidate const& c : n.candidates)
                        if (c.canBePrimary)
                            best = std::max(best, ValueOf(n, c));
                    m_best.push_back(best);
                }
            }

            std::vector<int> Run()
            {
                Visit(0, Score());
                bool const switchNow = m_bestScore.now - m_greedyScore.now > SWITCH_MARGIN;
                bool const switchLater = m_bestScore.now - m_greedyScore.now > -EPSILON &&
                                         m_bestScore.later - m_greedyScore.later > SWITCH_MARGIN;
                return switchNow || switchLater ? m_bestChoice : m_greedyChoice;
            }

        private:
            bool IsPrimary(ObjectGuid bot) const
            {
                return std::find(m_primaries.begin(), m_primaries.end(), bot) != m_primaries.end();
            }

            // Upper bound of what needs [i, end) can still add: per tier, the largest m_best of
            // as many needs as there are bots left (each bot answers one need at most).
            Score Bound(size_t i) const
            {
                std::vector<ObjectGuid> freeBots;
                std::vector<float> now, later;
                for (size_t j = i; j < m_needs.size(); ++j)
                {
                    (m_needs[j].now ? now : later).push_back(m_best[j]);
                    for (DutyCandidate const& c : m_needs[j].candidates)
                        if (c.canBePrimary && !IsPrimary(c.bot) &&
                            std::find(freeBots.begin(), freeBots.end(), c.bot) == freeBots.end())
                            freeBots.push_back(c.bot);
                }
                auto const topSum = [&freeBots](std::vector<float>& v)
                {
                    size_t const k = std::min(v.size(), freeBots.size());
                    std::partial_sort(v.begin(), v.begin() + k, v.end(), std::greater<float>());
                    float sum = 0.0f;
                    for (size_t j = 0; j < k; ++j)
                        sum += v[j];
                    return sum;
                };
                Score s;
                s.now = topSum(now);
                s.later = topSum(later);
                return s;
            }

            // The reserves' loss comes off the needs acting now (only they spend cooldowns).
            // Only at a leaf: the bounds above leave it out, so they stay upper bounds.
            void Leaf(Score score)
            {
                if (!m_reserves.empty())
                {
                    std::vector<Spend> spent;
                    for (size_t i = 0; i < m_needs.size(); ++i)
                        if (m_needs[i].now && m_choice[i] >= 0)
                        {
                            DutyCandidate const& c = m_needs[i].candidates[m_choice[i]];
                            spent.push_back({ c.bot, c.spellId, m_needs[i].target, m_needs[i].points });
                        }
                    score.now -= ReserveLoss(m_reserves, spent);
                }
                if (!m_haveGreedy)
                {
                    m_haveGreedy = true;
                    m_greedyScore = score;
                    m_greedyChoice = m_choice;
                    m_bestScore = score;
                    m_bestChoice = m_choice;
                    return;
                }
                if (Beats(score, m_bestScore))
                {
                    m_bestScore = score;
                    m_bestChoice = m_choice;
                }
            }

            void Visit(size_t i, Score score)
            {
                if (i == m_needs.size())
                {
                    Leaf(score);
                    return;
                }
                if (m_haveGreedy)
                {
                    if (++m_nodes > MAX_NODES)
                        return;
                    Score const bound = Bound(i);
                    Score optimistic;
                    optimistic.now = score.now + bound.now;
                    optimistic.later = score.later + bound.later;
                    if (!Beats(optimistic, m_bestScore))
                        return;
                }

                DutyNeed const& need = m_needs[i];
                if (IsBlocked(need, m_handled))
                {
                    m_choice[i] = BLOCKED;
                    Visit(i + 1, score);
                    return;
                }

                for (size_t k = 0; k < need.candidates.size(); ++k)
                {
                    DutyCandidate const& c = need.candidates[k];
                    if (!c.canBePrimary || IsPrimary(c.bot))
                        continue;
                    m_choice[i] = int(k);
                    m_primaries.push_back(c.bot);
                    size_t const marked = m_handled.size();
                    m_handled.insert(m_handled.end(), need.marks.begin(), need.marks.end());
                    Score next = score;
                    (need.now ? next.now : next.later) += ValueOf(need, c);
                    Visit(i + 1, next);
                    m_handled.resize(marked);
                    m_primaries.pop_back();
                }

                m_choice[i] = UNANSWERED;
                Visit(i + 1, score);
            }

            std::vector<DutyNeed> const& m_needs;
            std::vector<DutyReserve> const& m_reserves;
            std::vector<float> m_best;
            std::vector<int> m_choice;
            std::vector<ObjectGuid> m_primaries;
            std::vector<DutyKey> m_handled;
            uint32 m_nodes = 0;
            bool m_haveGreedy = false;
            Score m_greedyScore;
            std::vector<int> m_greedyChoice;
            Score m_bestScore;
            std::vector<int> m_bestChoice;
        };
    }

    float ReserveLoss(std::vector<DutyReserve> const& reserves, std::vector<Spend> const& spent)
    {
        if (spent.empty())
            return 0.0f;
        std::map<ObjectGuid, float> worstPerGroup;
        for (DutyReserve const& r : reserves)
        {
            if (r.covers.empty())
                continue;
            float share = 1.0f;
            float cheapest = std::numeric_limits<float>::max();
            bool allSpent = true;
            for (DutyReserve::Cover const& c : r.covers)
            {
                auto const s = std::find_if(spent.begin(), spent.end(), [&c](Spend const& x)
                    { return x.bot == c.bot && x.spellId == c.spellId; });
                if (s == spent.end())
                {
                    allSpent = false;
                    break;
                }
                share = std::min(share, s->target == r.group ? c.shareOnGroup : c.share);
                cheapest = std::min(cheapest, s->points);
            }
            if (!allSpent || r.points <= cheapest)
                continue;
            float& worst = worstPerGroup[r.group];
            worst = std::max(worst, (r.points - cheapest) * share);
        }
        float loss = 0.0f;
        for (auto const& g : worstPerGroup)
            loss += g.second;
        return loss;
    }

    std::vector<DutyResult> Allocate(std::vector<DutyNeed> needs, std::vector<DutyReserve> const& reserves)
    {
        std::stable_sort(needs.begin(), needs.end(), [](DutyNeed const& a, DutyNeed const& b)
        {
            if (a.now != b.now)
                return a.now;
            if (a.points != b.points)
                return a.points > b.points;
            return a.tieMs < b.tieMs;
        });
        for (DutyNeed& need : needs)
            std::stable_sort(need.candidates.begin(), need.candidates.end(), [](DutyCandidate const& a, DutyCandidate const& b)
            {
                return a.rank > b.rank;
            });

        std::vector<int> const choice = Search(needs, reserves).Run();

        struct Claim
        {
            DutyKind kind;
            ObjectGuid target;
        };
        std::map<ObjectGuid, Claim> primaries;     // bot -> the primary duty it holds
        for (size_t i = 0; i < needs.size(); ++i)
            if (choice[i] >= 0)
                primaries[needs[i].candidates[choice[i]].bot] = { needs[i].kind, needs[i].target };

        std::vector<DutyResult> results;
        for (size_t i = 0; i < needs.size(); ++i)
        {
            if (choice[i] == BLOCKED)
                continue;
            DutyNeed const& need = needs[i];

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

            size_t const chosen = choice[i] >= 0 ? size_t(choice[i]) : need.candidates.size();
            if (choice[i] >= 0)
            {
                r.primary = need.candidates[chosen].bot;
                r.primarySpellId = need.candidates[chosen].spellId;
            }

            // Better-ranked candidates, one entry per bot (a bot offers one candidate per
            // ability), not the chosen one.
            for (size_t k = 0; k < chosen; ++k)
            {
                DutyCandidate const& c = need.candidates[k];
                if (c.bot == r.primary || std::any_of(r.passed.begin(), r.passed.end(),
                        [&c](DutyPass const& q) { return q.bot == c.bot; }))
                    continue;
                DutyPass pass;
                pass.bot = c.bot;
                auto const held = primaries.find(c.bot);
                if (held != primaries.end())
                {
                    pass.reason = DutyPassReason::Busy;
                    pass.busyKind = held->second.kind;
                    pass.busyTarget = held->second.target;
                }
                else
                    pass.reason = c.canBePrimary ? DutyPassReason::LessWorth : DutyPassReason::NotPrimary;
                r.passed.push_back(pass);
            }
            // Declined (header): worth more unanswered, no backup either.
            if (r.primary.IsEmpty() && std::any_of(r.passed.begin(), r.passed.end(),
                    [](DutyPass const& p) { return p.reason == DutyPassReason::LessWorth; }))
                continue;

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

            if (!r.primary.IsEmpty() || !r.backup.IsEmpty())
                results.push_back(std::move(r));
        }
        return results;
    }
}
}
