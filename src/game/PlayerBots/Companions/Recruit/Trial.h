/*
 * Companion recruiting, 搭伙 (a trial partner; design .kiro/docs/2026-11-21-companion-recruiting.md
 * §2.2-2.3, workspace root): a stranger who agreed to play a while with the player. He fights,
 * loots and does quests like a companion, but asks for nothing (no class quests, town trips,
 * money) and has no affinity. After QUESTS_NEEDED quests done together (the player's turn-in)
 * or a dungeon cleared together (its last boss, after DUNGEON_OFFER_DELAY_MS for the looting)
 * he offers to stay: the player's yes promotes him (CompanionMgr::Promote). If he leaves the
 * world unpromoted, the trial is over (CompanionMgr::OnTrialLeave).
 *
 * Addon lines (Recruit.lua): PBTRIAL;<bot>;start | quest;<n>;<needed> | bond.
 */

#ifndef MANGOS_COMPANIONS_RECRUIT_TRIAL_H
#define MANGOS_COMPANIONS_RECRUIT_TRIAL_H

#include "Common.h"
#include "Companions/Talk/Talk.h"
#include <string>
#include <vector>

class Player;
class Map;
class Quest;

namespace Companions
{
namespace Trial
{
    uint32 constexpr QUESTS_NEEDED = 3;
    // After the turn-in: a moment, as a person would take before saying it.
    uint32 constexpr QUEST_OFFER_DELAY_MS = 3 * IN_MILLISECONDS;
    // After the last boss (user 2026-11-21: not before the body is looted).
    uint32 constexpr DUNGEON_OFFER_DELAY_MS = 25 * IN_MILLISECONDS;
    // A quest counts for a partner this close to the player (and on his map).
    float constexpr TOGETHER_YD = 100.0f;

    // Per party bot AI.
    struct State
    {
        bool told = false;      // he said what the deal is (PBTRIAL start)
    };

    // QuestEvents (the player's map thread): his trial partners near him who had the quest.
    void OnQuestRewarded(Player* player, Quest const* quest);
    // DungeonClears (the instance's map thread): the trial partners in it with their player.
    void OnDungeonEnd(Map* map);

    // PartyBotAI, out of combat: the first time he says what the deal is; a due offer: he says
    // it in party and opens the talk window of the player (Talk BondService options).
    void Update(Player* me, Player* leader, State& state, std::vector<std::string> const& tags);

    // Ready to be asked (enough done together): the talk window offers "bond".
    bool IsReady(uint32 charGuid);
    // His words and the two answers (bond:yes, bond:later).
    Talk::Page OfferPage(Talk::Context const& ctx);
    // The player said yes: he is a companion now (affinity starts, PBTRIAL bond, PBAFF).
    // Returns false when he was no trial partner.
    bool Bond(Player* bot, Player* player);
}
}

#endif
