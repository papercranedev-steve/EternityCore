/*
 * PartyBot Commander layer 1 (eyes): a tank's threat runway on an enemy, the seconds until
 * someone else on its list takes it (.kiro/docs/2026-11-11-aoe-threat.md 4.1, user 2026-11-12).
 * Replaces the "now" test of ThreatGap::Of (2026-11-08: enough to keep it through the healer's
 * next heal, nobody's output after that): the tank kept hitting one enemy far ahead and the
 * opening ended at a bar the first Blizzard ticks passed.
 *
 * For the tank T and every other party-side unit m on the list of `e`:
 *   f(m)     aggro factor: 1.1 in melee reach, 1.3 else (ThreatView)
 *   burst(m) a healer bot's next heal: healThreat / the enemies with the tank on their list
 *            (HostileRefManager::threatAssist splits it; none on an enemy that ignores assist
 *            threat: feared, confused, a breakable stun)
 *   lead(m)  T's threat x f(m) - (m's threat + burst(m))
 *   rate(m)  m's threat per second from now: the larger of its measured rate (ThreatEntry::
 *            threatPerSec) and a planned one (`planned`: the area spell the doctrine counts on)
 *   rate(T)  HOLDER_RATE_SHARE x T's measured rate on `e` while T hits `e`, else 0 (its rate
 *            is a jump of the last seconds, not what comes next: AreaValue's holder rate)
 *   runway   min over m of lead(m) / (rate(m) - rate(T) x f(m)); 0 when a lead is gone (or the
 *            tank has no threat at all), infinite when nobody gains on it.
 * Shortfall for `horizonSec`: threat T lacks for a runway of at least that long,
 *   max over m of (m's threat + burst(m) + max(0, rate(m) - rate(T) f(m)) x horizon) / f(m) - T,
 *   at least 1 while T has none. horizon 0 = the old ThreatGap::Of (survive the next heal).
 * `addTank`: threat T puts on `e` first (an area threat ability being valued).
 * Pure: WorldModel facts.
 */

#ifndef MANGOS_COMMANDER_THREAT_RUNWAY_H
#define MANGOS_COMMANDER_THREAT_RUNWAY_H

#include "Commander/Perception/WorldModel.h"
#include <vector>

namespace Commander
{
    // A member's planned output on an enemy: threat per second it will add (the area spell the
    // doctrine counts on: tickThreat / period, DoctrinePlan::areaThreat).
    struct PlannedThreat
    {
        ObjectGuid member;
        float perSec = 0.0f;
    };

    namespace ThreatRunway
    {
        // Look-ahead without a planned area spell: the length of a Blizzard (user 2026-11-12:
        // the runway is judged over the whole area spell).
        static constexpr float HORIZON_SEC = 8.0f;
        static constexpr float HOLDER_RATE_SHARE = 0.5f;

        struct Result
        {
            float sec = 0.0f;           // the runway (infinite: nobody gains on the tank)
            ObjectGuid taker;           // who takes it at the end of it (empty: nobody / a healer not on the list)
            float shortfall = 0.0f;     // threat the tank lacks for `horizonSec`
        };

        Result Of(WorldModel const& world, EnemyFacts const& e, ObjectGuid tank,
                  std::vector<PlannedThreat> const& planned, float horizonSec, float addTank = 0.0f);

        // Of with the tank adding nothing from now on (rate(T) = 0: it stops, a druid tank out
        // of its form, Perception/Values/FormExitCost.h). No horizon: shortfall 0.
        Result Idle(WorldModel const& world, EnemyFacts const& e, ObjectGuid tank);

        // One rival m of the tank on `e`: the seconds until m takes it (runway as above, for m
        // alone). Of's runway is the smallest of these.
        struct Rival
        {
            ObjectGuid member;
            float sec = 0.0f;
        };
        // Every rival with a finite runway (someone gaining on the tank, or a lead already gone).
        std::vector<Rival> Rivals(WorldModel const& world, EnemyFacts const& e, ObjectGuid tank,
                                  std::vector<PlannedThreat> const& planned, float addTank = 0.0f);

        // Threat per second `holder` adds to `e` from now: HOLDER_RATE_SHARE x its measured rate
        // while it hits `e`, else 0.
        float HolderRate(WorldModel const& world, EnemyFacts const& e, PartyMemberFacts const& holder);
    }
}

#endif
