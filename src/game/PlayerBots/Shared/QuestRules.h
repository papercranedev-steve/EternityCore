/*
 * Shared bot core (PlayerBots/Shared): why a character may not take a quest by the server's
 * rules, as one reason word. Nothing is sent to the character (the checks run with msg = false).
 * Used for a bot following its leader's accept (PlayerBotMgr::OnPlayerAcceptedQuest, the words go
 * to the addon in PBQUEST;skip) and for the leader himself (Life/Errands/QuestOffer: may he take
 * it, or should a bot ask to take it without him).
 */

#ifndef MANGOS_BOTSHARED_QUEST_RULES_H
#define MANGOS_BOTSHARED_QUEST_RULES_H

#include "Common.h"

class Player;
class Quest;

namespace BotShared
{
namespace QuestRules
{
    // Null: `player` may take `quest` now. Else, first that applies: "done" (handed in, not
    // repeatable), "have" (in the log), "level" (too low or too high), "class", "race", "prev"
    // (chain, exclusive group, breadcrumbs), "skill", "rep", "other" (any other rule),
    // "logfull", "bagsfull" (no room for the item the quest gives at start).
    char const* WhyNot(Player const* player, Quest const* quest);

    // The rules alone keep him from it: not his log or his bags, which he can make room in.
    bool Barred(char const* whyNot);
}
}

#endif
