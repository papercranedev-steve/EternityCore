/*
 * .partybot ai subcommands for value scoring (value-scoring doc section 7).
 * Called from HandlePartyBotAiCommand in CommanderCommands.cpp (world thread).
 */

#ifndef MANGOS_COMMANDER_PROFILE_COMMANDS_H
#define MANGOS_COMMANDER_PROFILE_COMMANDS_H

#include "Common.h"

class ChatHandler;
class Creature;
class Player;

namespace Commander
{
    class BotDirectives;

namespace ProfileCommands
{
    // .partybot ai profile            selected creature's profile
    bool ShowCreature(ChatHandler* handler, Creature* creature);
    // .partybot ai spell <id> [level] one spell's profile and amounts at a caster level
    bool ShowSpell(ChatHandler* handler, uint32 spellId, uint32 casterLevel);
    // .partybot ai rates              fight-wide rates of your party
    bool ShowRates(ChatHandler* handler, Player* player);
    // .partybot ai control            control spells of the bots and what each is worth now
    bool ShowControls(ChatHandler* handler, Player* player);
    // .partybot ai flee               enemies that flee at low health: when, who may join, cost
    bool ShowFlee(ChatHandler* handler, Player* player);
    // .partybot ai reload overrides
    void ReloadOverrides(ChatHandler* handler);
    // part of .partybot ai bot: the last `count` arbiter decisions, newest first
    void ShowDecisions(ChatHandler* handler, BotDirectives const& directives, uint32 count);
}
}

#endif
