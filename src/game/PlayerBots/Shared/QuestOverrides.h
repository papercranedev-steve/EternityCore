/*
 * Shared bot core (PlayerBots/Shared): per-quest exceptions for bots, from the world table
 * `custom_bot_quest` (one row per quest, missing table = no exceptions).
 *
 *  - follow_credit: quests whose objective bots cannot do themselves (a bear led into a trap,
 *    a flag placed on a tower): when a real player gets an objective count of such a quest
 *    (Player::KilledMonsterCredit / CastedCreatureOrGO call OnCredit), the bots of his group
 *    near him (group reward distance) holding it incomplete are brought up to his counts.
 *  - no_item_use: the quest's items are the player's to use (a boss summon, an event start):
 *    Life/Errands/QuestItemUse leaves them alone.
 *
 * Load: world thread, startup (PlayerBotMgr::Load). OnCredit: map thread of the player.
 */

#ifndef MANGOS_BOTSHARED_QUEST_OVERRIDES_H
#define MANGOS_BOTSHARED_QUEST_OVERRIDES_H

#include "Common.h"

class Player;

namespace BotShared
{
namespace QuestOverrides
{
    void Load();

    bool FollowsCredit(uint32 questId);
    bool NoItemUse(uint32 questId);

    // `player` just got an objective count (any quest). Bots and players without a group: nothing.
    void OnCredit(Player* player);
}
}

#endif
