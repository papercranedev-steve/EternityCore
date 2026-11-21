/*
 * What a companion lets the player do, by affinity (design .kiro/docs/2026-11-21-companion-affinity.md
 * §5, workspace root). One table: a new gate is one enum value and one row. Gates only hold back
 * what the player does to the companion (its money, items, bags) and its services; never how
 * well it plays (combat, buffs, heals, conjured water, asking for money).
 *
 * A gate is `ready` once the feature it holds back is wired to Allows; gates that are not ready
 * are neither announced nor sent to the addon (their feature does not exist yet).
 * Anyone that is not a companion (a plain party bot) is allowed everything.
 *
 * Any thread (the table is constant; affinity reads take CompanionMgr's lock).
 */

#ifndef MANGOS_COMPANIONS_AFFINITY_GATES_H
#define MANGOS_COMPANIONS_AFFINITY_GATES_H

#include "Common.h"
#include <vector>

namespace Companions
{
    namespace Gates
    {
        enum class Gate : uint8
        {
            TradeBasics,    // gold, food, drink, gear better than what it wears
            TradeAnything,  // any item (a lockbox for a rogue too)
            ManageBags,
            WarlockSummon,
            Crafting,       // with the player's materials
            MagePortal,
            TakeMoney,      // it gives / the player takes its money
            PickRewards,    // the player picks its quest rewards
            JoinAccount,    // recruited into the player's own account
        };

        struct Info
        {
            Gate gate;
            uint32 affinity;    // allowed from this affinity on
            uint32 classMask;   // 0 = any class, else 1 << (class - 1)
            bool ready;         // its feature asks Allows (see above)
            char const* key;    // phrase "affinity.unlock.<key>", addon id
            char const* label;  // what the player may do now (addon)
        };

        std::vector<Info> const& All();
        Info const& Of(Gate gate);

        // The companion allows it now (affinity >= the gate's); true for anyone not a companion.
        bool Allows(uint32 charGuid, Gate gate);

        // Ready gates for `classId` that open between `before` (exclusive) and `after` (inclusive).
        std::vector<Info const*> Opened(uint32 before, uint32 after, uint8 classId);
    }
}

#endif
