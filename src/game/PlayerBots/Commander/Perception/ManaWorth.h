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
    }
}

#endif
