/*
 * Shared bot facts (PlayerBots/Shared): which quest reward a bot would pick by itself.
 * The biggest upgrade (Shared/GearUpgrade.h: item levels gained), else the one that sells for
 * the most. Soul companions doc §4.3 (automatic reward choice).
 */

#ifndef MANGOS_BOTSHARED_QUEST_REWARDS_H
#define MANGOS_BOTSHARED_QUEST_REWARDS_H

#include "Common.h"

class Player;
class Quest;

namespace BotShared
{
    namespace QuestRewards
    {
        // Index into Quest::RewChoiceItemId (0 when there is no choice).
        uint32 BestChoice(Player const* player, Quest const* quest);
    }
}

#endif
