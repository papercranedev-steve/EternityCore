#include "Commander/Perception/Forecast/PullTimeline.h"
#include <algorithm>
#include <cmath>

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
}
}
