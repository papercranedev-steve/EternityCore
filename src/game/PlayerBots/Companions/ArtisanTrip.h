/*
 * A companion asks its leader to take it to an Artisan trainer (user 2026-11-21: the player
 * takes it there; design .kiro/docs/2026-11-21-companion-professions.md stage 5). Artisan is
 * taught by a few trainers out in the world (Shared/ArtisanTrainers), not in the capitals.
 *
 * Records like the class quests' (ClassQuestFlow::Record, same table custom_soulbot_request,
 * chain id = the trip quest id, QuestMirror::TripId(skill)):
 *   Waiting    may be asked (3 asks)
 *   Active     the player said yes: the trip quest is in his log, with a map flag at the nearest
 *              trainer on his continent; near a trainer the bot walks over and learns
 *              (Life::ArtisanTask through NpcVisit), then Done (thanks; no affinity)
 *   Exhausted  nobody took it: it learns Artisan by itself at its next login (when it has the fee)
 *   Done       learned (helped: the player took it)
 * When: once per visit of the leader to a capital, as the class quests: the Artisan rank is
 * green (skill 200, level, Expert known), it has the fee, no trip is Active, no other question
 * is open. Answers count as for class quests; the leader abandoning the quest is a "no".
 *
 * Map thread (the bot's AI). The leader's quest log only from the same map.
 */

#ifndef MANGOS_COMPANIONS_ARTISAN_TRIP_H
#define MANGOS_COMPANIONS_ARTISAN_TRIP_H

#include "Common.h"
#include "Companions/ClassQuests/ClassQuestFlow.h"
#include <map>
#include <string>
#include <vector>

class Player;

namespace Life
{
    class ArtisanTask;
}

namespace Companions
{
    namespace ArtisanTrip
    {
        struct State
        {
            bool loaded = false;
            std::map<uint32, ClassQuestFlow::Record> records;     // skill -> record
            bool leaderInCapital = false;
            bool askedThisVisit = false;
            uint32 requestId = 0;
            uint32 askingSkill = 0;
            uint32 lastCheckMs = 0;
            uint32 shownId = 0;             // the trip quest in the leader's log
            uint32 flagMapId = UINT32_MAX;  // the map the flag was put on
            bool logFullSaid = false;
        };

        // In the errand chain, every bot tick; never keeps the bot busy. Sets task.wanted.
        void Update(Player* me, Player* leader, State& state, Life::ArtisanTask& task, std::vector<std::string> const& tags);

        // An open question is cancelled (combat, a Commander plan).
        void Stop(State& state);
    }
}

#endif
