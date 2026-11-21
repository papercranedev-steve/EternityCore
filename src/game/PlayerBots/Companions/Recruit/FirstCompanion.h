/*
 * Companion recruiting: the player's first companion is a gift (design
 * .kiro/docs/2026-11-21-companion-recruiting.md §3, workspace root). When a new player (level up
 * to GIFT_MAX_LEVEL, never had a companion, never offered one) takes a quest, someone of his race who
 * can do what he cannot (heals for a fighter, a warrior for a healer) runs up to him, says why
 * and asks to join (Population BotKind::Herald, StrangerTalk). Yes: a companion at once, no
 * trial. Offered once per character, whatever he answers.
 */

#ifndef MANGOS_COMPANIONS_RECRUIT_FIRST_COMPANION_H
#define MANGOS_COMPANIONS_RECRUIT_FIRST_COMPANION_H

#include "Common.h"

class Player;
class Object;
class Quest;

namespace Companions
{
namespace FirstCompanion
{
    uint32 constexpr GIFT_MAX_LEVEL = 5;

    // QuestEvents (the player's map thread): remembered for Update.
    void OnQuestAccepted(Player* player, Quest const* quest, Object* giver);
    // CompanionMgr::Update (world thread): the herald of each new player who took a quest.
    void Update();
    // The class he comes as for `player` (0: none fits his race).
    uint8 ClassFor(Player const* player);
}
}

#endif
