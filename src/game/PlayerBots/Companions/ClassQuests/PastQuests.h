/*
 * What class quests a companion did before it met the player. Decided once, when it is
 * recruited (or, for a companion recruited before this existed, when it is first online):
 * every class quest chain for it (Shared/ClassQuestChains::ChainsFor) whose first quest it was
 * old enough for is rolled done or not, on that first quest. Done = the chain is in its history
 * (Shared/QuestGrant); an ability chain also with what its steps teach and give (totems), a gear
 * chain without its items. Not done = it will ask the player for help. Afterwards a class quest
 * ability it has without the quest is taken away (premade spec templates give them).
 *
 * Chance of done, by how far the bot is above the quest's level (the quest's XP falloff):
 *   gray quest, or 10+ levels above: done
 *   else 5% a level up to 5, plus half of the XP the quest no longer gives (Quest::XPValue):
 *   +1..+5: 5..25%, +6: 35%, +7: 45%, +8: 55%, +9: 65%
 * Seeded by the character and the chain: the same companion always gets the same past.
 * Design: .kiro/docs/2026-11-18-companion-class-quests.md §5 (workspace root).
 */

#ifndef MANGOS_COMPANIONS_PAST_QUESTS_H
#define MANGOS_COMPANIONS_PAST_QUESTS_H

#include "Common.h"

class Player;
class Quest;

namespace Companions
{
    namespace PastQuests
    {
        struct Result
        {
            uint32 done = 0;        // granted (or already done)
            uint32 notDone = 0;     // chains it will ask help for
            uint32 revoked = 0;     // abilities it had without the quest, taken away
        };

        // World thread, the bot in the world.
        Result Decide(Player* bot);

        // Chance 0..1 that a bot of `level` did `quest` already (see above).
        float ChanceDone(uint32 level, Quest const* quest);
    }
}

#endif
