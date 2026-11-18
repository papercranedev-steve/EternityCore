#include "Life/Errands/Trip/RouteOrder.h"
#include "Shared/WalkLength.h"
#include "Player.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace Life
{
namespace Trip
{
namespace RouteOrder
{
    namespace
    {
        using Point = BotShared::WalkLength::Point;

        Point PointOf(TripStop const& s)
        {
            Point p;
            p.x = s.x;
            p.y = s.y;
            p.z = s.z;
            return p;
        }

        float Straight(Point const& a, Point const& b)
        {
            float const dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
            return std::sqrt(dx * dx + dy * dy + dz * dz);
        }

        // Keeps the stops whose `after` is kept too, with the links renumbered.
        void Keep(TripPlanStops& plan, std::vector<bool> keep)
        {
            // A stop whose predecessor goes, goes too (the mailbox without its auction house).
            for (bool changed = true; changed;)
            {
                changed = false;
                for (size_t i = 0; i < plan.stops.size(); ++i)
                {
                    int const after = plan.stops[i].after;
                    if (keep[i] && after >= 0 && (size_t(after) >= plan.stops.size() || !keep[after]))
                    {
                        keep[i] = false;
                        changed = true;
                    }
                }
            }
            std::vector<int> newIndex(plan.stops.size(), -1);
            std::vector<TripStop> kept;
            for (size_t i = 0; i < plan.stops.size(); ++i)
                if (keep[i])
                {
                    newIndex[i] = int(kept.size());
                    kept.push_back(plan.stops[i]);
                }
            for (TripStop& s : kept)
                if (s.after >= 0)
                    s.after = newIndex[s.after];
            plan.stops = std::move(kept);
        }
    }

    bool Order(Player const* me, TripPlanStops& plan)
    {
        if (plan.stops.size() > MAX_STOPS)
        {
            std::vector<bool> keep(plan.stops.size(), false);
            std::fill(keep.begin(), keep.begin() + MAX_STOPS, true);
            Keep(plan, keep);
        }
        size_t const n = plan.stops.size();
        if (!n)
            return false;

        // Point 0 is the bot, point i + 1 is stop i.
        std::vector<Point> points;
        Point here;
        here.x = me->GetPositionX();
        here.y = me->GetPositionY();
        here.z = me->GetPositionZ();
        points.push_back(here);
        for (TripStop const& s : plan.stops)
            points.push_back(PointOf(s));

        std::vector<std::vector<float>> cost(n + 1, std::vector<float>(n + 1, 0.0f));
        std::vector<bool> reachable(n, false);
        for (size_t a = 0; a <= n; ++a)
            for (size_t b = 1; b <= n; ++b)
            {
                if (a == b)
                    continue;
                float const walk = BotShared::WalkLength::Between(me, points[a], points[b]);
                if (walk >= 0.0f)
                {
                    cost[a][b] = walk;
                    reachable[b - 1] = true;
                }
                else
                    cost[a][b] = Straight(points[a], points[b]) + UNREACHABLE_YD;
            }

        if (std::find(reachable.begin(), reachable.end(), false) != reachable.end())
        {
            // (Order again with what is left: the matrix has holes now)
            Keep(plan, reachable);
            return !plan.stops.empty() && Order(me, plan);
        }

        std::vector<size_t> order(n);
        for (size_t i = 0; i < n; ++i)
            order[i] = i;
        std::vector<size_t> best;
        float bestCost = std::numeric_limits<float>::max();
        std::vector<size_t> position(n);
        do
        {
            for (size_t i = 0; i < n; ++i)
                position[order[i]] = i;
            bool valid = true;
            for (size_t i = 0; i < n && valid; ++i)
            {
                int const after = plan.stops[i].after;
                valid = after < 0 || position[after] < position[i];
            }
            if (!valid)
                continue;
            float total = 0.0f;
            size_t from = 0;
            for (size_t i = 0; i < n && total < bestCost; ++i)
            {
                total += cost[from][order[i] + 1];
                from = order[i] + 1;
            }
            if (total < bestCost)
            {
                bestCost = total;
                best = order;
            }
        } while (std::next_permutation(order.begin(), order.end()));

        if (best.empty())
            return false;
        std::vector<int> newIndex(n, -1);
        std::vector<TripStop> sorted;
        for (size_t i : best)
        {
            newIndex[i] = int(sorted.size());
            sorted.push_back(plan.stops[i]);
        }
        for (TripStop& s : sorted)
            if (s.after >= 0)
                s.after = newIndex[s.after];
        plan.stops = std::move(sorted);
        return true;
    }
}
}
}
