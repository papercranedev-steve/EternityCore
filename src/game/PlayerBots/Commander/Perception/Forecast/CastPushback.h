/*
 * PartyBot Commander layer 1: how much the hits a member takes push back its spells with a cast
 * time (user 2026-11-28, rivendare-20261128-230646: Unholy Aura every 2 s made the priest's 2.5 s
 * Greater Heal take 3.3-4.4 s). Facts only. Doc .kiro/docs/2026-11-28-party-heal-renew.md
 * "Pushback".
 *
 * The core (Unit::DealDamage -> Spell::Delayed): each hit with damage left after the absorbs
 * (not a periodic tick) during a cast delays it by 1.0, 0.8, 0.6, 0.4, 0.2 s, then 0.2 s each
 * (Spell::GetNextDelayAtDamageMsTime, counted per cast), never back past the cast's start;
 * a fully absorbed hit delays nothing. (A trait's PushbackRate is not counted.)
 *
 * Hits: IncomingTimeline::Of (enemy swings, casts, spell list rows; not its periodic ticks,
 * Event::periodic: a channel's damage too, spread as ticks there) within its horizon, plus
 * the measured damage the model does not explain (Timeline::residualDps: an aura's pulses, a
 * script's area damage) as evenly spaced hits at the measured hit rate's share of it
 * (PartyMemberFacts::takenHitsPerSec x residual / measured dps). Periodic ticks among the
 * measured hits are counted as hits (DamageIntake does not tell them apart): the safe side for
 * the healer.
 */

#ifndef MANGOS_COMMANDER_CAST_PUSHBACK_H
#define MANGOS_COMMANDER_CAST_PUSHBACK_H

#include "Common.h"
#include <vector>

namespace Commander
{
    struct WorldModel;
    struct PartyMemberFacts;

namespace CastPushback
{
    struct Hit
    {
        float atSec = 0.0f;
        float damage = 0.0f;
    };

    // The hits `member` takes from now within `horizonSec` (header), sorted.
    std::vector<Hit> Hits(WorldModel const& world, PartyMemberFacts const& member, float horizonSec);

    // Seconds a cast of `castSec` started at `startSec` is pushed back by `hits`; `absorb` takes
    // whole hits first and is used up by what it takes.
    float DelaySec(std::vector<Hit> const& hits, float startSec, float castSec, float& absorb);

    // Casts of `castSec` back to back from now until `horizonSec`: the seconds the hits push
    // them back in total (casting time lost), with `absorb` in front.
    float LostSec(std::vector<Hit> const& hits, float castSec, float horizonSec, float absorb);
}
}

#endif
