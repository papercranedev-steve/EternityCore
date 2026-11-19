/*
 * A companion asks its leader for help with its class quests (design:
 * .kiro/docs/2026-11-18-companion-class-quests.md §3-§4, §6; workspace root).
 *
 * One record per chain (Shared/ClassQuestChains), in `custom_soulbot_request`:
 *   Waiting    may be asked (asks left, initially 3)
 *   Active     the player said yes: the leader's log follows it (MirrorSync), until done
 *   Exhausted  an ability chain nobody helped with: done by the bot itself at its next login
 *   Done       finished (helped: the player helped, affinity given)
 *   Dropped    a gear chain nobody helped with: never asked again
 * A chain without a record is Waiting with 3 asks.
 *
 * When: once per visit of the leader to a capital (entering a capital zone, or the bot logging
 * in there), at most one question per visit, while the bot can run errands (the errand chain of
 * PartyBotAI), is near the leader, nobody fights, no chain is Active and no other question is
 * open. Ability chains first, then by level. A visit in which it never got to ask is not used.
 * Answers: yes -> Active; no answer -> one ask less; no -> one ask left the first time, none
 * the second time. Leader abandons the mirror (or the bot its quest) during Active: a "no".
 *
 * Map thread (the bot's AI). The records are the AI's (loaded at its first update).
 */

#ifndef MANGOS_COMPANIONS_CLASS_QUEST_FLOW_H
#define MANGOS_COMPANIONS_CLASS_QUEST_FLOW_H

#include "Common.h"
#include "Companions/ClassQuests/MirrorSync.h"
#include <map>
#include <string>
#include <vector>

class Player;

namespace Companions
{
    namespace ClassQuestFlow
    {
        enum class RecordState : uint8 { Waiting = 0, Active = 1, Exhausted = 2, Done = 3, Dropped = 4 };

        uint8 const ASKS = 3;

        struct Record
        {
            RecordState state = RecordState::Waiting;
            uint8 asksLeft = ASKS;
            bool declined = false;
            bool helped = false;
        };

        struct State
        {
            bool loaded = false;
            std::map<uint32, Record> records;   // chain id -> record
            bool leaderInCapital = false;
            bool askedThisVisit = false;
            bool forceVisit = false;            // `.partybot classquests visit`: a visit now, anywhere
            uint32 requestId = 0;
            uint32 askingChain = 0;
            uint32 lastCheckMs = 0;
            bool spellsChanged = false;         // the AI rebuilds its spell data (TakeSpellsChanged)
        };

        // In the errand chain, every bot tick. Never keeps the bot busy (it asks while following).
        void Update(Player* me, Player* leader, State& state, MirrorSync::Link& link, std::vector<std::string> const& tags);

        // What MirrorSync::Update reported (any tick).
        void OnMirrorEvent(Player* me, Player* leader, State& state, MirrorSync::Link& link,
            MirrorSync::Event event, std::vector<std::string> const& tags);

        // An open question is cancelled (combat, a Commander plan): the visit is not used.
        void Stop(State& state);

        bool TakeSpellsChanged(State& state);

        // The quests the bot should take from their givers now: the next step of its Active chain
        // when it is not in its log yet (Life/Errands/QuestPickUp). Empty: none.
        std::vector<uint32> WantedPickUps(Player const* me, State const& state);

        // The record of a chain (a default one when there is none).
        Record RecordOf(State const& state, uint32 chainId);
        char const* StateName(RecordState state);
    }
}

#endif
