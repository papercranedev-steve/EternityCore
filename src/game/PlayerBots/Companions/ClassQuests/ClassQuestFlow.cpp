#include "Companions/ClassQuests/ClassQuestFlow.h"
#include "Companions/CompanionMgr.h"
#include "Shared/ClassQuestChains.h"
#include "Shared/QuestMirror.h"
#include "Shared/BotChat.h"
#include "Life/Requests/RequestBoard.h"
#include "Life/Requests/Phrases.h"
#include "Player.h"
#include "ObjectMgr.h"
#include "QuestDef.h"
#include "WorldSession.h"
#include "Database/DatabaseEnv.h"
#include "DBCStores.h"
#include "Timer.h"
#include "Log.h"
#include <algorithm>
#include <tuple>

namespace Companions
{
    namespace ClassQuestFlow
    {
        namespace
        {
            namespace Chains = BotShared::ClassQuestChains;
            namespace Requests = Life::Requests;

            uint32 const CHECK_MS = 1000;
            float const ASK_RANGE_YD = 30.0f;

            bool Persists(Player const* me)
            {
                return !sCompanionMgr.IsTemporary(me->GetGUIDLow());
            }

            void Save(Player const* me, uint32 chainId, Record const& r)
            {
                if (!Persists(me))
                    return;
                CharacterDatabase.PExecute("REPLACE INTO `custom_soulbot_request` (`char_guid`, `chain_id`, `state`, `asks_left`, `declined`, `helped`)"
                    " VALUES (%u, %u, %u, %u, %u, %u)", me->GetGUIDLow(), chainId, uint32(r.state), uint32(r.asksLeft),
                    uint32(r.declined), uint32(r.helped));
            }

            void Say(Player* me, char const* key, std::vector<std::string> const& tags, Life::Phrases::Vars const& vars = {})
            {
                std::string const text = Life::Phrases::Say(key, tags, vars);
                if (!text.empty())
                    BotShared::BotChat::SayToParty(me, text);
            }

            // The chain's last quest's title as `reader` reads it.
            std::string TitleFor(Player const* reader, Chains::Chain const& chain)
            {
                Quest const* quest = sObjectMgr.GetQuestTemplate(chain.id);
                if (!quest)
                    return std::to_string(chain.id);
                int const idx = reader && reader->GetSession() ? reader->GetSession()->GetSessionDbLocaleIndex() : -1;
                if (QuestLocale const* locale = sObjectMgr.GetQuestLocale(chain.id))
                    if (idx >= 0 && idx < int(locale->Title.size()) && !locale->Title[idx].empty())
                        return locale->Title[idx];
                return quest->GetTitle();
            }

            bool InCapital(Player const* player)
            {
                AreaEntry const* zone = AreaEntry::GetById(player->GetZoneId());
                return zone && (zone->Flags & AREA_FLAG_CAPITAL);
            }

            // The first step of the chain the bot has not turned in yet.
            Quest const* NextStep(Player const* me, Chains::Chain const& chain)
            {
                for (Quest const* step : Chains::StepsFor(me, chain))
                    if (!me->GetQuestRewardStatus(step->GetQuestId()))
                        return step;
                return nullptr;
            }

            void Finish(Player* me, Player* leader, State& state, uint32 chainId, std::vector<std::string> const& tags)
            {
                Record& r = state.records[chainId];
                r.state = RecordState::Done;
                r.helped = true;
                Save(me, chainId, r);
                Chains::Chain const* chain = Chains::Find(chainId);
                uint32 const points = chain ? chain->affinity : 0;
                uint32 const now = sCompanionMgr.AddAffinity(me->GetGUIDLow(), points, "class quest");
                Life::Phrases::Vars vars;
                vars["affinity"] = std::to_string(now);
                Say(me, "classquest.thanks", tags, vars);
                if (leader)
                    BotShared::BotChat::SystemTo(leader, "PBAFF;" + std::string(me->GetName()) + ";" + std::to_string(now) +
                        ";" + std::to_string(points) + ";classquest");
            }

            // No answer, a "no", or the help given up: the rules of §4.
            void CountAsk(Player* me, State& state, uint32 chainId, bool declined, std::vector<std::string> const& tags)
            {
                Record& r = state.records[chainId];
                if (!declined)
                    r.asksLeft = r.asksLeft ? uint8(r.asksLeft - 1) : 0;
                else if (!r.declined)
                {
                    r.declined = true;
                    r.asksLeft = 1;
                }
                else
                    r.asksLeft = 0;

                r.state = RecordState::Waiting;
                Chains::Chain const* chain = Chains::Find(chainId);
                if (!r.asksLeft)
                {
                    bool const ability = chain && chain->kind == Chains::Kind::Ability;
                    r.state = ability ? RecordState::Exhausted : RecordState::Dropped;
                    Say(me, ability ? "classquest.giveup" : "classquest.dropped", tags);
                }
                else if (declined)
                    Say(me, "classquest.declined", tags);
                Save(me, chainId, r);
            }

            void Load(Player* me, Player* leader, State& state, MirrorSync::Link& link, std::vector<std::string> const& tags)
            {
                state.loaded = true;
                if (Persists(me))
                {
                    std::unique_ptr<QueryResult> result(CharacterDatabase.PQuery(
                        "SELECT `chain_id`, `state`, `asks_left`, `declined`, `helped` FROM `custom_soulbot_request` WHERE `char_guid` = %u",
                        me->GetGUIDLow()));
                    if (result)
                    {
                        do
                        {
                            Field* f = result->Fetch();
                            Record r;
                            uint8 const s = f[1].GetUInt8();
                            r.state = s <= uint8(RecordState::Dropped) ? RecordState(s) : RecordState::Waiting;
                            r.asksLeft = f[2].GetUInt8();
                            r.declined = f[3].GetUInt8() != 0;
                            r.helped = f[4].GetUInt8() != 0;
                            state.records[f[0].GetUInt32()] = r;
                        } while (result->NextRow());
                    }
                }

                // Before the player's help ran out last time: done by itself now (§4), the first
                // one told in party chat.
                bool told = false;
                for (auto& itr : state.records)
                {
                    Chains::Chain const* chain = Chains::Find(itr.first);
                    if (!chain)
                        continue;
                    Record& r = itr.second;
                    if (r.state == RecordState::Exhausted)
                    {
                        if (!Chains::IsDone(me, *chain))
                            Chains::GrantDone(me, *chain);
                        r.state = RecordState::Done;
                        r.helped = false;
                        Save(me, itr.first, r);
                        state.spellsChanged = true;
                        if (!told)
                        {
                            Life::Phrases::Vars vars;
                            vars["quest"] = TitleFor(leader, *chain);
                            Say(me, "classquest.selfdone", tags, vars);
                            told = true;
                        }
                        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[Companion] '%s' did class quest chain %u by itself.", me->GetName(), itr.first);
                    }
                    else if (r.state == RecordState::Active)
                    {
                        if (Chains::IsDone(me, *chain))
                            Finish(me, leader, state, itr.first, tags);
                        else if (!link.questId)
                            if (Quest const* step = NextStep(me, *chain))
                                MirrorSync::Follow(link, step->GetQuestId(), chain->id);
                    }
                }
            }

            bool AnyActive(State const& state)
            {
                for (auto const& itr : state.records)
                    if (itr.second.state == RecordState::Active)
                        return true;
                return false;
            }

            // The chain to ask about now, nullptr: none.
            Chains::Chain const* PickChain(Player const* me, State const& state)
            {
                Chains::Chain const* best = nullptr;
                for (Chains::Chain const* chain : Chains::ChainsFor(me))
                {
                    if (Chains::IsDone(me, *chain) || me->GetLevel() < chain->minLevel)
                        continue;
                    if (chain->after && !me->GetQuestRewardStatus(chain->after))
                        continue;
                    Record const r = RecordOf(state, chain->id);
                    if (r.state != RecordState::Waiting || !r.asksLeft)
                        continue;
                    if (!NextStep(me, *chain))
                        continue;
                    auto key = [](Chains::Chain const* c)
                    {
                        return std::make_tuple(c->kind == Chains::Kind::Ability ? 0 : 1, c->minLevel, c->id);
                    };
                    if (!best || key(chain) < key(best))
                        best = chain;
                }
                return best;
            }

            void Ask(Player* me, Player* leader, State& state, Chains::Chain const& chain, std::vector<std::string> const& tags)
            {
                Life::Phrases::Vars vars;
                vars["quest"] = TitleFor(leader, chain);
                vars["leader"] = leader->GetName();
                Requests::Ask ask;
                ask.intent = Requests::Intent::ClassQuest;
                ask.text = Life::Phrases::Say(chain.kind == Chains::Kind::Ability ? "classquest.ask" : "classquest.askgear", tags, vars);
                ask.options = {
                    { "yes", Life::Phrases::Word("classquest.opt.yes", "yes") },
                    { "no",  Life::Phrases::Word("classquest.opt.no", "no") },
                };
                ask.fallback = "no";    // (an Expired request is told apart from a "no" by its state)
                state.requestId = sRequestBoard.Open(me, leader, ask);
                if (!state.requestId)
                    return;
                state.askingChain = chain.id;
                state.askedThisVisit = true;
                state.forceVisit = false;
                BotShared::BotChat::SayToParty(me, ask.text);
            }

            void PollAnswer(Player* me, Player* leader, State& state, MirrorSync::Link& link, std::vector<std::string> const& tags)
            {
                Requests::Status const status = sRequestBoard.Poll(state.requestId);
                if (status.state == Requests::State::Open)
                    return;
                uint32 const chainId = state.askingChain;
                uint32 const id = state.requestId;
                state.requestId = 0;
                state.askingChain = 0;
                Chains::Chain const* chain = Chains::Find(chainId);
                if (status.state == Requests::State::None || !chain)
                {
                    state.askedThisVisit = false;   // dropped: not a real ask
                    return;
                }

                if (status.state == Requests::State::Answered && status.answer == "yes")
                {
                    sRequestBoard.Close(id, Requests::Outcome::Accepted);
                    Record& r = state.records[chainId];
                    r.state = RecordState::Active;
                    Save(me, chainId, r);
                    MirrorSync::Stop(leader, link);
                    if (Quest const* step = NextStep(me, *chain))
                        MirrorSync::Follow(link, step->GetQuestId(), chainId);
                    Say(me, "classquest.accepted", tags);
                    return;
                }

                bool const declined = status.state == Requests::State::Answered;
                sRequestBoard.Close(id, declined ? Requests::Outcome::Declined : Requests::Outcome::Expired);
                CountAsk(me, state, chainId, declined, tags);
            }
        }

        Record RecordOf(State const& state, uint32 chainId)
        {
            auto const itr = state.records.find(chainId);
            return itr != state.records.end() ? itr->second : Record();
        }

        char const* StateName(RecordState state)
        {
            switch (state)
            {
                case RecordState::Waiting:   return "waiting";
                case RecordState::Active:    return "active";
                case RecordState::Exhausted: return "exhausted";
                case RecordState::Done:      return "done";
                case RecordState::Dropped:   return "dropped";
            }
            return "?";
        }

        void Update(Player* me, Player* leader, State& state, MirrorSync::Link& link, std::vector<std::string> const& tags)
        {
            // (the leader may be a headless dev console character: IsBot, still a "player")
            if (!me || !leader)
                return;
            if (!state.loaded)
                Load(me, leader, state, link, tags);

            if (state.requestId)
            {
                PollAnswer(me, leader, state, link, tags);
                return;
            }

            uint32 const now = WorldTimer::getMSTime();
            if (state.lastCheckMs && WorldTimer::getMSTimeDiff(state.lastCheckMs, now) < CHECK_MS)
                return;
            state.lastCheckMs = now;

            bool const inCapital = leader->IsInWorld() && InCapital(leader);
            if (inCapital && !state.leaderInCapital)
                state.askedThisVisit = false;   // a new visit
            state.leaderInCapital = inCapital;

            if ((!inCapital && !state.forceVisit) || state.askedThisVisit || AnyActive(state))
                return;
            if (me->IsInCombat() || leader->IsInCombat() || !me->IsWithinDistInMap(leader, ASK_RANGE_YD))
                return;
            if (!sRequestBoard.MayAsk(me->GetObjectGuid(), Requests::Intent::ClassQuest))
                return;
            if (Chains::Chain const* chain = PickChain(me, state))
                Ask(me, leader, state, *chain, tags);
            else
                state.forceVisit = false;
        }

        void OnMirrorEvent(Player* me, Player* leader, State& state, MirrorSync::Link& link,
            MirrorSync::Event event, std::vector<std::string> const& tags)
        {
            if (!me || event == MirrorSync::Event::None)
                return;
            // The chain the link was on (ended links are reset: look for the Active record).
            uint32 chainId = link.chainId;
            if (!chainId)
                for (auto const& itr : state.records)
                    if (itr.second.state == RecordState::Active)
                        chainId = itr.first;
            auto const itr = state.records.find(chainId);
            if (itr == state.records.end() || itr->second.state != RecordState::Active)
                return;     // a dev `.partybot mirror follow`, not a help the player agreed to

            switch (event)
            {
                case MirrorSync::Event::StepDone:
                    Say(me, "classquest.step", tags);
                    break;
                case MirrorSync::Event::ChainDone:
                    Finish(me, leader, state, chainId, tags);
                    break;
                case MirrorSync::Event::LeaderAbandoned:
                case MirrorSync::Event::BotAbandoned:
                {
                    // The help is off: the bot drops the quest too (it cannot do it alone).
                    if (Chains::Chain const* chain = Chains::Find(chainId))
                        for (Quest const* step : Chains::StepsFor(me, *chain))
                        {
                            uint16 const slot = me->FindQuestSlot(step->GetQuestId());
                            if (slot < MAX_QUEST_LOG_SIZE)
                                me->RemoveQuestAtSlot(slot);
                        }
                    MirrorSync::Stop(leader, link);
                    CountAsk(me, state, chainId, true, tags);
                    break;
                }
                case MirrorSync::Event::None:
                    break;
            }
        }

        void Stop(State& state)
        {
            if (!state.requestId)
                return;
            sRequestBoard.Close(state.requestId, Requests::Outcome::Cancelled);
            state.requestId = 0;
            state.askingChain = 0;
            state.askedThisVisit = false;
        }

        bool TakeSpellsChanged(State& state)
        {
            bool const changed = state.spellsChanged;
            state.spellsChanged = false;
            return changed;
        }
    }
}
