#include "Commander/Behaviors/Movement/HumanFollow.h"
#include "MotionMaster.h"
#include "Player.h"
#include "TargetedMovementGenerator.h"
#include "MoveSpline.h"
#include "Timer.h"
#include "Utilities/Random.h"
#include <algorithm>
#include <cmath>

namespace Commander
{
namespace HumanFollow
{
    namespace
    {
        // Deterministic 0..1 values from the character guid (splitmix64 steps).
        class Dice
        {
        public:
            explicit Dice(uint64 seed) : m_state(seed) {}

            float Next()
            {
                uint64 z = (m_state += 0x9E3779B97F4A7C15ULL);
                z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
                z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
                z ^= z >> 31;
                return float(z >> 40) / float(1ULL << 24);
            }

            float Range(float lo, float hi) { return lo + (hi - lo) * Next(); }

        private:
            uint64 m_state;
        };
    }

    Personality Of(Player const* bot)
    {
        Dice dice(bot->GetObjectGuid().GetRawValue());
        Personality p;
        float const side = dice.Range(SIDE_MIN, SIDE_MAX) * (dice.Next() < 0.5f ? -1.0f : 1.0f);
        p.offset.dist = dice.Range(DIST_MIN, DIST_MAX);
        p.offset.angle = float(M_PI) + side;
        p.reactionMs = uint32(dice.Range(float(REACTION_MIN_MS), float(REACTION_MAX_MS)));
        p.startSlack = dice.Range(START_SLACK_MIN, START_SLACK_MAX);
        // Fix: the stop band inside the start band. A bot with stopSlack above its startSlack
        // stopped early where it already counted as left behind and started again: stop, step,
        // stop.
        p.stopSlack = std::min(dice.Range(STOP_SLACK_MIN, STOP_SLACK_MAX), p.startSlack - BAND_GAP);
        p.faceArc = dice.Next() < ATTENTIVE_SHARE ? ATTENTIVE_ARC : LAZY_ARC;
        p.turnDelayMs = uint32(dice.Range(float(TURN_DELAY_MIN_MS), float(TURN_DELAY_MAX_MS)));
        p.idleAfterMs = uint32(dice.Range(float(IDLE_AFTER_MIN_MS), float(IDLE_AFTER_MAX_MS)));
        return p;
    }

    namespace
    {
        FollowSpot::Offset Drifted(Personality const& p)
        {
            FollowSpot::Offset o;
            o.dist = std::max(DIST_MIN, std::min(DIST_MAX, p.offset.dist + frand(-DRIFT_DIST, DRIFT_DIST)));
            o.angle = p.offset.angle + frand(-DRIFT_ANGLE, DRIFT_ANGLE);
            return o;
        }

        bool IsStill(Unit const* u)
        {
            return !u->IsMoving() && u->movespline->Finalized();
        }

        bool Due(uint32 now, uint32 at)
        {
            return int32(now - at) >= 0;
        }
    }

    bool Tick(Player* bot, Unit const* leader, IdleState& s, bool following, FollowSpot::Offset& next)
    {
        uint32 const now = WorldTimer::getMSTime();
        if (!following || bot->IsInCombat() || leader->IsInCombat())
        {
            if (s.sat && bot->IsSittingDown())
                bot->SetStandState(UNIT_STAND_STATE_STAND);
            s = IdleState();
            return false;
        }

        // Where the leader stands, and since when.
        bool const still = IsStill(leader) &&
            (!s.leaderStill || leader->GetDistance2d(s.stillX, s.stillY) <= STILL_YD);
        if (!still)
            s.leaderStill = false;
        else if (!s.leaderStill)
        {
            s.leaderStill = true;
            s.stillSinceMs = now;
            s.stillX = leader->GetPositionX();
            s.stillY = leader->GetPositionY();
            s.nextActMs = 0;
        }
        if (!bot->IsSittingDown())
            s.sat = false;

        Personality const p = Of(bot);
        if (!s.leaderStill)
        {
            if (!s.nextActMs)
                s.nextActMs = now + urand(DRIFT_EVERY_MIN_MS, DRIFT_EVERY_MAX_MS);
            if (!Due(now, s.nextActMs))
                return false;
            s.nextActMs = 0;
            next = Drifted(p);
            return true;
        }

        uint32 const stood = WorldTimer::getMSTimeDiff(s.stillSinceMs, now);
        if (stood < p.idleAfterMs)
            return false;
        if (!s.nextActMs)
        {
            s.nextActMs = now + urand(IDLE_EVERY_MIN_MS, IDLE_EVERY_MAX_MS);
            return false;
        }
        if (!Due(now, s.nextActMs))
            return false;
        s.nextActMs = now + urand(IDLE_EVERY_MIN_MS, IDLE_EVERY_MAX_MS);

        // Only a bot standing on its own: not walking, casting or already sitting (eating).
        if (!IsStill(bot) || bot->IsNonMeleeSpellCasted(false) || bot->IsSittingDown())
            return false;

        uint32 const roll = urand(0, 99);
        if (roll < LOOK_CHANCE)
        {
            bot->SetFacingTo(bot->GetAngle(leader) + frand(-LOOK_TURN, LOOK_TURN));
            return false;
        }
        if (roll < LOOK_CHANCE + STEP_CHANCE || stood < SIT_AFTER_MS)
        {
            next = Drifted(p);
            return true;
        }
        bot->SetStandState(UNIT_STAND_STATE_SIT);
        s.sat = true;
        return false;
    }

    float Heading(Player const* bot, Unit const* leader)
    {
        MotionMaster const* mm = bot->GetMotionMaster();
        if (!mm->empty() && mm->GetCurrentMovementGeneratorType() == FOLLOW_MOTION_TYPE)
            if (auto const* follow = dynamic_cast<FollowMovementGenerator<Player> const*>(mm->GetCurrent()))
                if (follow->HasStyle() && follow->GetTarget() == leader)
                    return follow->GetHeading();
        return leader->GetOrientation();
    }

    FollowStyle StyleOf(Player const* bot, float heading)
    {
        Personality const p = Of(bot);
        FollowStyle s;
        s.heading = heading;
        s.reactionMs = p.reactionMs;
        s.startSlack = p.startSlack;
        s.stopSlack = p.stopSlack;
        s.faceArc = p.faceArc;
        s.turnDelayMs = p.turnDelayMs;
        return s;
    }
}
}
