#include "Commander/Perception/Forecast/FearRisk.h"
#include "Commander/Perception/Spatial/PackResolver.h"
#include <algorithm>
#include <cmath>

namespace Commander
{
namespace FearRisk
{
    namespace
    {
        float const DEFAULT_RUN_SPEED = 7.0f;   // a creature's base run speed, when the facts have none

        bool MayAnswer(PackCreature const& c)
        {
            return !c.noAggro && !c.tappedByOther;
        }

        bool IsMeleeChaser(PartyMemberFacts const& m)
        {
            return m.alive && (m.isPet || m.role == ROLE_TANK || m.role == ROLE_MELEE_DPS);
        }

        // The longest leg the server picks at `d` yards from the one who feared it (plus the
        // random point around the end, within the path limit).
        float LongestLeg(float d)
        {
            float leg = d <= QUIET_FAR_YD ? QUIET_LEG_YD : FAR_LEG_YD;
            if (d < QUIET_NEAR_YD)
                leg = std::max(leg, NEAR_LEG_FACTOR * (QUIET_NEAR_YD - d));
            return std::min(LEG_PATH_YD, leg + RANDOM_POINT_YD);
        }
    }

    float RunYd(float sec, float speed, float fromCasterYd)
    {
        if (sec <= 0.0f || speed <= 0.0f)
            return 0.0f;
        float t = 0.0f;
        float run = 0.0f;
        float d = fromCasterYd;
        while (t < sec)
        {
            float const leg = LongestLeg(d);
            float const legSec = leg / speed;
            if (t + legSec >= sec)
                return run + (sec - t) * speed;
            run += leg;
            d += leg;
            t += legSec + MIN_PAUSE_SEC;
        }
        return run;
    }

    std::vector<PackRisk> Of(WorldModel const& world, Position3 const& caster, float radius, float sec,
                             float speedMult, std::vector<EnemyFacts const*> const& feared)
    {
        struct Run
        {
            Position3 from;
            float reachYd = 0.0f;
            bool chased = false;
        };
        std::vector<Run> runs;
        for (EnemyFacts const* e : feared)
        {
            if (!e)
                continue;
            Run r;
            r.from = e->pos;
            float const speed = (e->speed > 0.0f ? e->speed : DEFAULT_RUN_SPEED) * std::max(1.0f, speedMult);
            r.reachYd = RunYd(sec, speed, PackResolver::Distance(e->pos, caster));
            for (PartyMemberFacts const& m : world.party)
                if (IsMeleeChaser(m) && m.victim == e->guid)
                    r.chased = true;
            runs.push_back(r);
        }

        std::vector<PackRisk> out;
        for (PackFacts const& pack : world.packs)
        {
            PackRisk risk;
            risk.pack = &pack;
            for (PackCreature const& c : pack.members)
            {
                if (radius > 0.0f && !c.noAggro && PackResolver::Distance(c.pos, caster) <= radius)
                    risk.certain = true;
                if (risk.certain || !MayAnswer(c))
                    continue;
                for (Run const& r : runs)
                {
                    float const answerYd = r.chased ? std::max(CALL_YD, c.reach + CHASE_MARGIN_YD) : CALL_YD;
                    if (PackResolver::Distance(c.pos, r.from) <= r.reachYd + answerYd)
                        risk.chance = 1.0f;
                }
            }
            if (risk.certain)
                risk.chance = 1.0f;
            if (risk.chance > 0.0f)
                out.push_back(risk);
        }
        return out;
    }
}
}
