#include "Commander/Behaviors/Movement/HumanFollow.h"
#include "Commander/Team.h"
#include "MotionMaster.h"
#include "Player.h"
#include "TargetedMovementGenerator.h"
#include "MoveSpline.h"
#include "Timer.h"
#include "Utilities/Random.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

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
        // `who` follows `leader` (any follow, styled or not).
        FollowMovementGenerator<Player> const* FollowOf(Player const* who, Unit const* leader)
        {
            MotionMaster const* mm = who->GetMotionMaster();
            if (mm->empty() || mm->GetCurrentMovementGeneratorType() != FOLLOW_MOTION_TYPE)
                return nullptr;
            auto const* follow = dynamic_cast<FollowMovementGenerator<Player> const*>(mm->GetCurrent());
            return follow && follow->GetTarget() == leader ? follow : nullptr;
        }

        // A point in the leader's frame: x along `heading`.
        struct Spot
        {
            float x = 0.0f;
            float y = 0.0f;
        };

        Spot SpotOf(FollowSpot::Offset const& o)
        {
            return { o.dist * std::cos(o.angle), o.dist * std::sin(o.angle) };
        }

        float Clamp01(float v)
        {
            return std::max(0.0f, std::min(1.0f, v));
        }

        // The zone behind the leader for this bot: where the team's other members are going to
        // stand (the follow point of those that follow `leader`, else where they stand) and how
        // deep it is for the number following.
        struct Zone
        {
            std::vector<Spot> taken;
            float depth = DIST_MAX;

            // Distance <-> share 0..1 of the zone's area (area-uniform: as many places near the
            // inner edge as at the outer one).
            float DistOf(float share) const
            {
                return std::sqrt(DIST_MIN * DIST_MIN + Clamp01(share) * (depth * depth - DIST_MIN * DIST_MIN));
            }
            float ShareOf(float dist) const
            {
                return Clamp01((dist * dist - DIST_MIN * DIST_MIN) / (depth * depth - DIST_MIN * DIST_MIN));
            }
        };

        Zone ZoneOf(Player const* bot, Unit const* leader, float heading)
        {
            Zone z;
            uint32 followers = 1;
            float const c = std::cos(-heading);
            float const s = std::sin(-heading);
            for (Player const* m : Team::Members(bot))
            {
                // (other maps belong to other threads)
                if (m == bot || m == leader || !m->IsInWorld() || m->GetMap() != bot->GetMap() || !m->IsAlive())
                    continue;
                if (FollowMovementGenerator<Player> const* f = FollowOf(m, leader))
                {
                    ++followers;
                    FollowSpot::Offset o;
                    o.dist = f->GetOffset();
                    o.angle = f->GetAngle();
                    z.taken.push_back(SpotOf(o));
                    continue;
                }
                float const dx = m->GetPositionX() - leader->GetPositionX();
                float const dy = m->GetPositionY() - leader->GetPositionY();
                z.taken.push_back({ dx * c - dy * s, dx * s + dy * c });
            }
            z.depth = std::max(DIST_MAX, std::sqrt(DIST_MIN * DIST_MIN + 2.0f * followers * AREA_PER_FOLLOWER / ZONE_ARC));
            return z;
        }

        // Dart throwing around (`share`, `angle`): the first candidate MIN_GAP from every taken
        // spot, else the one farthest from them.
        FollowSpot::Offset Place(Zone const& z, float share, float angle, float shareSpread, float angleSpread)
        {
            float const lo = float(M_PI) - ZONE_ARC / 2.0f;
            float const hi = float(M_PI) + ZONE_ARC / 2.0f;
            // (0..2pi first: FollowSpot's candidates may come as -pi..pi)
            angle = std::fmod(angle, 2.0f * float(M_PI));
            if (angle < 0.0f)
                angle += 2.0f * float(M_PI);

            FollowSpot::Offset best;
            float bestGap2 = -1.0f;
            for (uint32 i = 0; i < PLACE_TRIES; ++i)
            {
                FollowSpot::Offset o;
                o.dist = z.DistOf(share + frand(-shareSpread, shareSpread));
                o.angle = std::max(lo, std::min(hi, angle + frand(-angleSpread, angleSpread)));
                Spot const p = SpotOf(o);
                float gap2 = std::numeric_limits<float>::max();
                for (Spot const& t : z.taken)
                    gap2 = std::min(gap2, (p.x - t.x) * (p.x - t.x) + (p.y - t.y) * (p.y - t.y));
                if (gap2 >= MIN_GAP * MIN_GAP)
                    return o;
                if (gap2 > bestGap2)
                {
                    best = o;
                    bestGap2 = gap2;
                }
            }
            return best;
        }

        // Around the current place (a drift, a step).
        FollowSpot::Offset Drifted(Player const* bot, Unit const* leader, FollowSpot::Offset const& current)
        {
            Zone const z = ZoneOf(bot, leader, Heading(bot, leader));
            return Place(z, z.ShareOf(current.dist), current.angle, DRIFT_SPREAD, DRIFT_ANGLE);
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

    FollowSpot::Offset Reroll(Player const* bot, Unit const* leader, float heading)
    {
        Personality const p = Of(bot);
        Zone const z = ZoneOf(bot, leader, heading);
        // (the personality's distance as a share of the 1-follower zone: the same part of a
        // deeper zone with more followers)
        float const share = (p.offset.dist - DIST_MIN) / (DIST_MAX - DIST_MIN);
        return Place(z, share, p.offset.angle, PLACE_SPREAD, PLACE_ANGLE);
    }

    bool Tick(Player* bot, Unit const* leader, IdleState& s, bool following,
              FollowSpot::Offset const& current, FollowSpot::Offset& next)
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

            // A new place at each stop after a walk (not after a step on the spot; the first
            // stop of a follow only marks the spot: StartFollowing rerolled already).
            bool const walked = s.stopKnown &&
                leader->GetDistance2d(s.stopX, s.stopY) >= REROLL_MOVE_YD;
            bool const firstStop = !s.stopKnown;
            if (walked || firstStop)
            {
                s.stopKnown = true;
                s.stopX = s.stillX;
                s.stopY = s.stillY;
            }
            if (walked)
            {
                next = Reroll(bot, leader, Heading(bot, leader));
                return true;
            }
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
            next = Drifted(bot, leader, current);
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
            next = Drifted(bot, leader, current);
            return true;
        }
        bot->SetStandState(UNIT_STAND_STATE_SIT);
        s.sat = true;
        return false;
    }

    float Heading(Player const* bot, Unit const* leader)
    {
        if (FollowMovementGenerator<Player> const* follow = FollowOf(bot, leader))
            if (follow->HasStyle())
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
