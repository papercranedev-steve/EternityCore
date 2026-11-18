/*
 * Bot daily life (PlayerBots/Life), errand: make what the bot keeps in its bags instead of
 * buying it (Shared/Supplies::ToMake): a mage's conjured food and water, a rogue's poison
 * vials. Soul companions doc §4.4 (stage S4). Any bot kind can run it; who does is the
 * caller's choice (PartyBotAI: companions, which get no free supplies).
 *
 * Update (bot update, out of combat), one cast at a time (Shared/Crafting::Craft):
 *   - nothing while it may not run an errand (Life/Errands/ErrandCommon::MayRun: combat,
 *     mounted, eating or drinking), while it or the anchor moves (the party walks on), or
 *     while it casts something else;
 *   - the first need it can make now; a cast that costs power keeps MIN_POWER_PCT of it after
 *     the cast (resting refills the rest), except for goods it has none of (the first water
 *     is what lets a mage drink);
 *   - a recipe that fails is not tried again for FAIL_BACKOFF_MS (no reagents, bags full);
 *   - true while its cast goes on (the caller keeps it still).
 *
 * Map thread of the bot.
 */

#ifndef MANGOS_LIFE_CRAFT_RUN_H
#define MANGOS_LIFE_CRAFT_RUN_H

#include "Common.h"
#include <map>

class Player;

namespace Life
{
    // Per bot (a member of its AI). Plain data.
    struct CraftRunState
    {
        uint32 castingSpell = 0;                // the recipe being cast, 0 = none
        uint32 nextTryMs = 0;
        std::map<uint32, uint32> failedUntilMs; // recipe -> not tried before
    };

    namespace CraftRun
    {
        constexpr uint32 TRY_MS = 1000;
        constexpr uint32 FAIL_BACKOFF_MS = 60000;
        constexpr float MIN_POWER_PCT = 50.0f;

        // `anchor`: the party leader (null = none). True: the bot casts a recipe.
        bool Update(Player* me, CraftRunState& state, Player const* anchor);

        // Stops its cast (combat, a Commander plan, the bot no longer crafts).
        void Stop(Player* me, CraftRunState& state);
    }
}

#endif
