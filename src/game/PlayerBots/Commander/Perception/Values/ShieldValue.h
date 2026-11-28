/*
 * PartyBot Commander layer 1: what an absorb shield put on a member now is worth (user
 * 2026-11-28: a priest's Power Word: Shield on itself under Unholy Aura, a mage's Ice Barrier
 * whenever it has one; party heal doc "Pushback"). Pure: facts.
 *
 * Over HORIZON_SEC (the hits are only known that far, IncomingTimeline):
 *   absorbed   the hits CastPushback::Hits sees, beyond the absorbs already on it, at most the
 *              shield's amount: health the healers need not give back
 *   pushback   the member casts its usual spell (TypicalCastSec) back to back: the casting
 *              time the hits take with its absorbs now minus with the shield on top
 *              (CastPushback::LostSec)
 *   benefit    Value::Points(absorbed) + Value::LostAction(member, pushback seconds)
 * The caller adds the shield's cost (its GCD, its mana: ManaWorth) and compares.
 * Known limits: a healer is taken to cast back to back (true under raid-wide damage, not while
 * it rests); the shield's life beyond the horizon is not counted (the safe side).
 */

#ifndef MANGOS_COMMANDER_SHIELD_VALUE_H
#define MANGOS_COMMANDER_SHIELD_VALUE_H

#include "Common.h"

namespace Commander
{
    struct WorldModel;
    struct PartyMemberFacts;

namespace ShieldValue
{
    static constexpr float HORIZON_SEC = 8.0f;     // = IncomingTimeline::HORIZON_SEC

    struct Result
    {
        float absorbed = 0.0f;
        float pushbackSec = 0.0f;
        float benefit = 0.0f;
    };

    // A shield of `absorb` put on `member` now.
    Result Of(WorldModel const& world, PartyMemberFacts const& member, float absorb);

    // The cast time of the spell `member` casts most (seconds, 0 = none with a cast time): a
    // damage dealer's filler, a healer's longest heal (HealCapacity rates).
    float TypicalCastSec(PartyMemberFacts const& member);
}
}

#endif
