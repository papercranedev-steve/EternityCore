/*
 * A real player accepted or turned in a quest (called from Player::AddQuest / RewardQuest; bots
 * are not reported). The core only knows this file; who listens is set at startup (companion
 * recruiting, Companions/Recruit: the first companion comes when the player takes his first
 * quest, a trial partner counts quests done together; design
 * .kiro/docs/2026-11-21-companion-recruiting.md, workspace root).
 *
 * Map thread of the player. Listeners run there too.
 */

#ifndef MANGOS_BOTSHARED_QUEST_EVENTS_H
#define MANGOS_BOTSHARED_QUEST_EVENTS_H

#include "Common.h"

class Player;
class Object;
class Quest;

namespace BotShared
{
    namespace QuestEvents
    {
        // `giver`: who gave it (may be null: shared, an item).
        using AcceptedListener = void (*)(Player* player, Quest const* quest, Object* giver);
        using RewardedListener = void (*)(Player* player, Quest const* quest);
        // One listener each (set at startup, before players log in).
        void SetListeners(AcceptedListener accepted, RewardedListener rewarded);

        void OnAccepted(Player* player, Quest const* quest, Object* giver);
        void OnRewarded(Player* player, Quest const* quest);
    }
}

#endif
