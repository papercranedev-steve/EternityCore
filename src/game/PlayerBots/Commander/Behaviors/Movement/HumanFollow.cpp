#include "Commander/Behaviors/Movement/HumanFollow.h"
#include "MotionMaster.h"
#include "Player.h"
#include "TargetedMovementGenerator.h"
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
        p.stopSlack = dice.Range(STOP_SLACK_MIN, STOP_SLACK_MAX);
        p.faceArc = dice.Next() < ATTENTIVE_SHARE ? ATTENTIVE_ARC : LAZY_ARC;
        p.turnDelayMs = uint32(dice.Range(float(TURN_DELAY_MIN_MS), float(TURN_DELAY_MAX_MS)));
        return p;
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
