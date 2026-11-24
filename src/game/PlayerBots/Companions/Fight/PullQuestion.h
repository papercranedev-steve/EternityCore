/*
 * Companions, fights: the pull stance's question to the player (Commander/Coordination/
 * Situation/Pull/PullQuestions.h; design .kiro/docs/2026-11-24-pull-stance.md §4). The brain writes
 * the question into the team plan (it may not reach the popup, Life/Requests); the asking tank
 * shows it here and gives the answer back (CommanderMgr::AnswerPullQuestion).
 *
 * The tank says the question in party chat and opens the popup (Requests::Intent::Pull,
 * options yes / no, no answer = no). A question that went away meanwhile (answered elsewhere,
 * the stance turned passive) closes its popup.
 *
 * Map thread of the bot (each bot runs it; only the asker does anything).
 */

#ifndef MANGOS_COMPANIONS_FIGHT_PULL_QUESTION_H
#define MANGOS_COMPANIONS_FIGHT_PULL_QUESTION_H

#include "Common.h"
#include <string>
#include <vector>

class Player;
class TeamBotAI;

namespace Companions
{
namespace PullQuestion
{
    struct State
    {
        uint32 questionId = 0;  // the team's question this bot asked last
        uint32 requestId = 0;   // its popup while open
    };

    void Update(TeamBotAI* ai, Player* leader, State& state, std::vector<std::string> const& tags);
}
}

#endif
