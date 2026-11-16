/*
 * PartyBot Commander layer 1 (eyes): what mana a party member spends is worth, in points
 * (crowd control doc 2026-11-07 section 2.3; also for team rescue, todo item 20). Pure, on
 * WorldModel facts.
 *
 * Mana is worth what it would have done later in this fight, times how scarce it is:
 *   healer          mana x healing per mana                    -> Value::Points
 *                   (PartyMemberFacts::healRates average, else ManaBudget::FALLBACK_HEAL_PER_MANA)
 *                   scarcity from the healer's tension (same terms as ManaBudgetBehavior::
 *                   ComputeBudget: incoming damage over the fight left, headroom of the members
 *                   being hit, shared between healers, against its mana)
 *   damage dealer   seconds of filler casting the mana pays for (mana / fillerManaPerSec)
 *                   -> Value::LostAction; tension = fight left / seconds its whole mana lasts
 *   scarcity = max(SCARCITY_FLOOR, ManaBudget::Scarcity(tension)): 0 at tension 1 (enough
 *   mana for this fight), 1 at tension 2. The floor stands for drinking longer after the fight.
 *   warlock         at most what tapping it back costs (Life Tap, since 2026-11-08, user: a
 *                   warlock's mana is dear but less than a mage's): Value::Points(mana /
 *                   tapManaPerHealth health) + LostAction(GCD x mana / tapManaPerCast).
 * Members without a mana pool, rage and energy costs: 0.
 */

#ifndef MANGOS_COMMANDER_MANA_WORTH_H
#define MANGOS_COMMANDER_MANA_WORTH_H

#include "Common.h"

namespace Commander
{
    struct WorldModel;
    struct PartyMemberFacts;

    namespace ManaWorth
    {
        // Untested guess (crowd control doc section 7).
        static constexpr float SCARCITY_FLOOR = 0.1f;

        // ManaBudget tension of this member's mana in the current fight (0 = no mana use).
        float Tension(WorldModel const& world, PartyMemberFacts const& member);

        // Points `mana` of `member` is worth now.
        float Points(WorldModel const& world, PartyMemberFacts const& member, float mana);

        // A fight not started yet (a pull being planned, FightLoad estimate): its length and
        // the damage per second it brings, instead of the live rates (0 out of combat).
        struct Fight
        {
            float sec = 0.0f;
            float incomingDps = 0.0f;
        };
        float Points(WorldModel const& world, PartyMemberFacts const& member, float mana, Fight const& fight);

        // Losing `mana` (an enemy mana drain or leech) leaves a healer bot short for this
        // fight: after it, its tension is above CRITICAL_TENSION (its mana no longer pays for
        // the healing the fight still needs). Amounts below CRITICAL_MIN_SHARE of its pool
        // never are. Interrupting or dispelling such a drain is urgent like a lethal cast
        // (fight-20261116-232401: two Soul Tap channels took the paladin's mana, ~2100 each,
        // while every interrupt and Cleanse waited behind emergency heals; the party wiped).
        static constexpr float CRITICAL_TENSION = 1.0f;
        static constexpr float CRITICAL_MIN_SHARE = 0.05f;
        bool Critical(WorldModel const& world, PartyMemberFacts const& member, float mana);
    }
}

#endif
