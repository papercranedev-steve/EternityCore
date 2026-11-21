/*
 * World population bots: a real player talks with one of the people of the world (design
 * .kiro/docs/2026-11-21-companion-recruiting.md §2.1, §3, workspace root). The same talk window
 * as with a companion (Companions/Talk, PBTALK lines), other words:
 *  - he greets the player, who can ask him to play along (meet:join) or what he is doing
 *    (meet:chat). Asked to join, he thinks it over: not above the player's level, then his
 *    personality (once per player while he is online). A yes makes him a trial partner (搭伙,
 *    the manager recruits him: PopulationMgr::UpdateRecruits).
 *  - a player who just invites him gets no yes: he declines the invite and asks why (OnInvite).
 *  - the herald (BotKind::Herald, the player's first companion) says why he came and offers to
 *    join (first:yes / first:no): a yes makes him a companion at once.
 *
 * Map thread of the bot (chat commands, his AI).
 */

#ifndef MANGOS_POPULATION_STRANGER_TALK_H
#define MANGOS_POPULATION_STRANGER_TALK_H

#include "Common.h"
#include <string>
#include <vector>

class ChatHandler;
class Player;

namespace Population
{
namespace StrangerTalk
{
    // Farther than a talk window (he may have been invited from a little away).
    float constexpr MEET_YD = 30.0f;

    // A population bot (PopulationBotAI).
    bool Is(Player* bot);

    // .partybot talk <him> [option]: the page, or what the option does.
    bool Handle(ChatHandler& handler, Player* player, Player* bot, char const* option);

    // A real player invited him (the invite is declined already): he asks why, a page with the
    // options; too far for that: a whisper.
    void OnInvite(Player* player, Player* bot);

    // "等你到 N 级再来找我吧": he is above the player's level.
    std::string TooHighLine(Player* bot, std::vector<std::string> const& tags);

    // The herald's words, one per call (`index` from 0); after the last one the offer page goes
    // to the player and it returns false.
    bool HeraldSpeaks(Player* player, Player* bot, uint32 index);
    // Yes (on the page, or by inviting him) / no answer in time.
    void HeraldAccepted(Player* player, Player* bot);
    void HeraldGivesUp(Player* player, Player* bot);
}
}

#endif
