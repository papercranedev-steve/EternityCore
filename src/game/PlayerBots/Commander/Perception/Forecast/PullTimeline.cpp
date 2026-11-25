#include "Commander/Perception/Forecast/PullTimeline.h"
#include <algorithm>
#include <cmath>
#include <utility>

namespace Commander
{
namespace PullTimeline
{
    namespace
    {
        float Distance2d(Position3 const& a, Position3 const& b)
        {
            float const dx = a.x - b.x, dy = a.y - b.y;
            return std::sqrt(dx * dx + dy * dy);
        }
    }

    Result Of(Input const& in)
    {
        Result r;
        bool const walkIn = in.opener == Opener::WalkIn;
        float walkYd = in.pathYd >= 0.0f ? in.pathYd : Distance2d(in.tankAt, in.openFrom) * PATH_FACTOR;
        if (walkIn)
            walkYd = std::max(0.0f, walkYd - (in.charges ? CHARGE_MAX_YD : MELEE_REACH));
        r.walkSec = walkYd / RUN_YARDS_PER_SEC;

        switch (in.opener)
        {
            case Opener::WalkIn: r.openSec = r.walkSec; break;
            case Opener::Shot:   r.openSec = r.walkSec + OPENER_SEC; break;
            case Opener::Face:   r.openSec = r.walkSec + FACE_NOTICE_SEC; break;
            case Opener::Ambush: r.openSec = std::max(r.walkSec, in.ambushWaitSec); break;
        }

        // Walk-in and ambush: the pack is on the tank when the pull starts, also while it
        // walks the pack back.
        float toContact = 0.0f;
        if (in.opener == Opener::Shot || in.opener == Opener::Face)
        {
            float const gapYd = std::max(0.0f, Distance2d(in.packAt, in.openFrom) - MELEE_REACH);
            if (!in.returnToSpot)
                toContact = gapYd / ENEMY_RUN_YARDS_PER_SEC;
            else
            {
                // The pack catches the tank walking back, or meets it at the fight spot.
                float const catchUp = gapYd / std::max(0.1f, ENEMY_RUN_YARDS_PER_SEC - BACK_YARDS_PER_SEC);
                float const backSec = Distance2d(in.openFrom, in.fightSpot) / BACK_YARDS_PER_SEC;
                float const runSec = std::max(0.0f, Distance2d(in.packAt, in.fightSpot) - MELEE_REACH) /
                                     ENEMY_RUN_YARDS_PER_SEC;
                toContact = std::min(catchUp, std::max(backSec, runSec));
            }
        }
        r.contactSec = r.openSec + toContact;
        r.damageSec = r.contactSec + THREAT_LEAD_SEC;
        return r;
    }

    namespace
    {
        Position3 Lerp(Position3 const& a, Position3 const& b, float f)
        {
            Position3 p;
            p.x = a.x + (b.x - a.x) * f;
            p.y = a.y + (b.y - a.y) * f;
            p.z = a.z + (b.z - a.z) * f;
            return p;
        }

        // Points every WALK_SAMPLE_YD along `line` up to `stopYd`, each with its distance.
        void Sample(std::vector<Position3> const& line, float stopYd,
                    std::vector<std::pair<Position3, float>>& out)
        {
            if (line.empty())
                return;
            out.emplace_back(line.front(), 0.0f);
            float walked = 0.0f;
            float next = WALK_SAMPLE_YD;
            for (size_t i = 1; i < line.size() && walked < stopYd; ++i)
            {
                float const seg = Distance2d(line[i - 1], line[i]);
                while (seg > 0.0f && next <= walked + seg && next <= stopYd)
                {
                    out.emplace_back(Lerp(line[i - 1], line[i], (next - walked) / seg), next);
                    next += WALK_SAMPLE_YD;
                }
                if (walked + seg >= stopYd && stopYd > walked && seg > 0.0f)
                    out.emplace_back(Lerp(line[i - 1], line[i], (stopYd - walked) / seg), stopYd);
                walked += seg;
            }
        }

        float Length(std::vector<Position3> const& line)
        {
            float yd = 0.0f;
            for (size_t i = 1; i < line.size(); ++i)
                yd += Distance2d(line[i - 1], line[i]);
            return yd;
        }

        void Add(std::vector<Zone>& out, ZoneKind kind, Position3 const& at, float fromSec, float toSec, char const* what)
        {
            Zone z;
            z.kind = kind;
            z.center = at;
            z.fromSec = std::max(0.0f, fromSec);
            z.toSec = toSec;
            z.what = what;
            out.push_back(z);
        }
    }

    std::vector<Zone> Presence(Input const& in, Result const& r, std::vector<Position3> const& walk,
                               std::vector<Position3> const& packSpots, Position3 const& partyAt)
    {
        std::vector<Zone> out;
        bool const walkIn = in.opener == Opener::WalkIn;

        // The tank's walk, its time spread like walkSec (Of: the same length less the reach).
        std::vector<Position3> line = walk;
        if (line.empty())
            line = { in.tankAt, in.openFrom };
        float const lineYd = Length(line);
        float const stopYd = walkIn ? std::max(0.0f, lineYd - (in.charges ? CHARGE_MAX_YD : MELEE_REACH)) : lineYd;
        std::vector<std::pair<Position3, float>> points;
        Sample(line, stopYd, points);
        for (auto const& p : points)
        {
            float const t = stopYd > 0.0f ? p.second / stopYd * r.walkSec : 0.0f;
            Add(out, ZoneKind::Walk, p.first, t - WALK_WINDOW_SEC, t + WALK_WINDOW_SEC, "walk");
        }

        // The pulled creatures where they stand, until they leave after the opener.
        for (Position3 const& s : packSpots)
            Add(out, ZoneKind::Place, s, 0.0f, r.openSec + PACK_LEAVE_SEC, "pack");

        bool const fromAfar = in.opener != Opener::WalkIn;
        if (in.returnToSpot)
        {
            // From where it opened (walk-in: the pack) back to the fight spot, the pack on it.
            Position3 const backFrom = walkIn ? in.packAt : in.openFrom;
            if (fromAfar)
                Add(out, ZoneKind::Place, in.openFrom, r.walkSec, r.openSec + WALK_WINDOW_SEC, "open spot");
            std::vector<Position3> const back = { backFrom, in.fightSpot };
            float const backYd = Length(back);
            std::vector<std::pair<Position3, float>> backPoints;
            Sample(back, backYd, backPoints);
            for (auto const& p : backPoints)
            {
                float const t = r.openSec + p.second / BACK_YARDS_PER_SEC;
                Add(out, ZoneKind::Place, p.first, t - WALK_WINDOW_SEC, t + WALK_WINDOW_SEC, "walk back");
            }
            Add(out, ZoneKind::Place, in.fightSpot, r.openSec + backYd / BACK_YARDS_PER_SEC, FOREVER, "fight");
        }
        else if (fromAfar && in.opener != Opener::Ambush)
            Add(out, ZoneKind::Place, in.openFrom, r.walkSec, FOREVER, "fight");    // they come to the open spot
        else
        {
            // Walk-in: where the pack stands; ambush: the tank waits on the way and meets it there.
            if (in.opener == Opener::Ambush)
                Add(out, ZoneKind::Place, in.openFrom, r.walkSec, FOREVER, "open spot");
            Add(out, ZoneKind::Place, in.packAt, r.openSec, FOREVER, "fight");
        }

        Add(out, ZoneKind::Party, partyAt, 0.0f, FOREVER, "party");
        return out;
    }

    char const* KindName(ZoneKind kind)
    {
        switch (kind)
        {
            case ZoneKind::Walk:  return "walk";
            case ZoneKind::Place: return "place";
            case ZoneKind::Party: return "party";
        }
        return "?";
    }
}
}
