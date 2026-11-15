/*
 * PartyBot Commander layer 1: do the others kill an enemy before a bot's cast lands on it?
 * A Frostbolt, Shadow Bolt or Aimed Shot started on an enemy about to die hits nothing; the
 * bot is better off on another enemy (DamageTargets), not starting it (Constraints::WastedCast)
 * or stopping it (Behaviors/Combat/FinishCast.h).
 *
 * Every time here is in ms from WorldModel::refreshedAtMs (the facts' moment); live callers
 * add the time since the refresh. The others' damage until `landMs`, the safe side everywhere
 * (what is not sure is not counted, so a bot rather stays than leaves an enemy alive):
 *   pending hit  a member's direct damage cast or bolt in the air on the enemy
 *                (PartyMemberFacts::pendingHit) that lands before `landMs`: its direct damage.
 *                Only those landing first: of two casts on a dying enemy the later one is the
 *                one wasted, so two bots never both leave counting on each other.
 *   rate         a member that is on the enemy now (PartyMemberFacts::victim): its measured
 *                damage per second on it (EnemyFacts::dealtBy) from when its cast in progress
 *                ends (its pending hit lands) until `landMs`. Nobody measured = nothing.
 *   running      while the enemy flees, melee and pets have lost it (FleePredictor's rule):
 *                only ranged members count.
 *   players      a player's damage counts PLAYER_TRUST (he may stop or switch; the bots' own
 *                rules are known). His pets too.
 * Finished: that damage reaches COVER_MARGIN x the enemy's health. Periodic damage already on
 * it (a DoT from a member now on another enemy) is not counted.
 */

#ifndef MANGOS_COMMANDER_FINISH_FORECAST_H
#define MANGOS_COMMANDER_FINISH_FORECAST_H

#include "Commander/Perception/WorldModel.h"

namespace Commander
{
namespace FinishForecast
{
    // Resists, misses, a heal on it, a melee swing a little late: the others' damage must
    // exceed the health by this much.
    static constexpr float COVER_MARGIN = 1.25f;
    static constexpr float PLAYER_TRUST = 0.5f;

    // `self`'s next filler (PartyMemberFacts::fillerCastMs) on `enemy` lands this long after the
    // refresh: its pending hit on it if it is casting at it, else what it is casting now (if
    // anything) plus the filler's cast and flight. 0 = no filler with a cast time.
    uint32 FillerLandMs(PartyMemberFacts const& self, EnemyFacts const& enemy);
    // Flight of a spell with `speed` (yards per second) from `from` to `enemy`'s edge.
    uint32 FlightMs(Position3 const& from, EnemyFacts const& enemy, float speed);

    // Damage the party but `self` deals on `enemy` within `landMs` (header).
    float OthersWithin(WorldModel const& world, EnemyFacts const& enemy, ObjectGuid self, uint32 landMs);
    // The others kill `enemy` before `self`'s hit landing `landMs` after the refresh.
    bool Finished(WorldModel const& world, EnemyFacts const& enemy, ObjectGuid self, uint32 landMs);
}
}

#endif
