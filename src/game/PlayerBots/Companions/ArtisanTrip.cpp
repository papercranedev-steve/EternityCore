#include "Companions/ArtisanTrip.h"
#include "Companions/CompanionMgr.h"
#include "Life/Errands/MerchantTasks.h"
#include "Life/Requests/Phrases.h"
#include "Life/Requests/RequestBoard.h"
#include "Shared/ArtisanTrainers.h"
#include "Shared/BotChat.h"
#include "Shared/Professions.h"
#include "Shared/QuestMirror.h"
#include "Player.h"
#include "ObjectMgr.h"
#include "WorldSession.h"
#include "Chat.h"
#include "Database/DatabaseEnv.h"
#include "DBCStores.h"
#include "Timer.h"
#include "Log.h"

namespace Companions
{
    namespace ArtisanTrip
    {
        namespace
        {
            namespace Requests = Life::Requests;
            namespace Trainers = BotShared::ArtisanTrainers;
            namespace Mirror = BotShared::QuestMirror;
            using ClassQuestFlow::Record;
            using ClassQuestFlow::RecordState;

            uint32 const CHECK_MS = 1000;
            float const ASK_RANGE_YD = 30.0f;
            uint32 const AFFINITY = 5;      // the player took it there

            bool Persists(Player const* me)
            {
                return !sCompanionMgr.IsTemporary(me->GetGUIDLow());
            }

            void Save(Player const* me, uint32 skill, Record const& r)
            {
                if (!Persists(me))
                    return;
                CharacterDatabase.PExecute("REPLACE INTO `custom_soulbot_request` (`char_guid`, `chain_id`, `state`, `asks_left`, `declined`, `helped`)"
                    " VALUES (%u, %u, %u, %u, %u, %u)", me->GetGUIDLow(), Trainers::QuestOf(skill), uint32(r.state), uint32(r.asksLeft),
                    uint32(r.declined), uint32(r.helped));
            }

            void Say(Player* me, char const* key, std::vector<std::string> const& tags, Life::Phrases::Vars const& vars)
            {
                std::string const text = Life::Phrases::Say(key, tags, vars);
                if (!text.empty())
                    BotShared::BotChat::SayToParty(me, text);
            }

            int LocaleIdx(Player const* reader)
            {
                return reader && reader->GetSession() ? reader->GetSession()->GetSessionDbLocaleIndex() : -1;
            }

            std::string SkillName(uint32 skill, Player const* reader)
            {
                SkillLineEntry const* line = sSkillLineStore.LookupEntry(skill);
                if (!line)
                    return std::string();
                LocaleConstant const locale = reader && reader->GetSession() ? reader->GetSession()->GetSessionDbcLocale() : LOCALE_enUS;
                char const* name = line->name[locale] && *line->name[locale] ? line->name[locale] : line->name[0];
                return name ? name : "";
            }

            // skill, trainer (the nearest one for the leader), gold.
            Life::Phrases::Vars VarsFor(Player* me, Player const* leader, uint32 skill)
            {
                Life::Phrases::Vars vars;
                vars["skill"] = SkillName(skill, leader);
                vars["leader"] = leader ? leader->GetName() : "";
                vars["gold"] = Life::Phrases::Money(Trainers::RankCost(skill));
                Player const* where = leader ? leader : me;
                std::vector<Trainers::Spawn> const spawns = Trainers::For(me, skill, where->GetMapId(), where->GetPositionX(), where->GetPositionY());
                vars["trainer"] = spawns.empty() ? std::string() : Trainers::NameOf(spawns.front().entry, LocaleIdx(leader));
                return vars;
            }

            bool InCapital(Player const* player)
            {
                AreaEntry const* zone = AreaEntry::GetById(player->GetZoneId());
                return zone && (zone->Flags & AREA_FLAG_CAPITAL);
            }

            bool CanPay(Player const* me, uint32 skill)
            {
                return me->GetMoney() >= Trainers::RankCost(skill);
            }

            Record RecordOf(State const& state, uint32 skill)
            {
                auto const itr = state.records.find(skill);
                return itr != state.records.end() ? itr->second : Record();
            }

            uint32 ActiveSkill(State const& state)
            {
                for (auto const& itr : state.records)
                    if (itr.second.state == RecordState::Active)
                        return itr.first;
                return 0;
            }

            // Learns it without a trainer (what it did by itself while away), paying the fee.
            bool LearnAlone(Player* me, uint32 skill)
            {
                uint32 const spell = Trainers::RankSpell(skill);
                if (!spell || !BotShared::Professions::ArtisanAvailable(me, skill) || !CanPay(me, skill))
                    return false;
                me->LearnSpell(spell, false);
                if (uint32 const cost = Trainers::RankCost(skill))
                    me->ModifyMoney(-int32(cost));
                return true;
            }

            void Load(Player* me, Player* leader, State& state, std::vector<std::string> const& tags)
            {
                state.loaded = true;
                if (Persists(me))
                {
                    std::unique_ptr<QueryResult> result(CharacterDatabase.PQuery(
                        "SELECT `chain_id`, `state`, `asks_left`, `declined`, `helped` FROM `custom_soulbot_request`"
                        " WHERE `char_guid` = %u AND `chain_id` >= %u", me->GetGUIDLow(), Mirror::TRIP_OFFSET));
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
                            state.records[f[0].GetUInt32() - Mirror::TRIP_OFFSET] = r;
                        } while (result->NextRow());
                    }
                }

                // Nobody took it last time: it went by itself.
                for (auto& itr : state.records)
                {
                    Record& r = itr.second;
                    if (r.state != RecordState::Exhausted || !LearnAlone(me, itr.first))
                        continue;
                    r.state = RecordState::Done;
                    r.helped = false;
                    Save(me, itr.first, r);
                    Say(me, "artisan.selfdone", tags, VarsFor(me, leader, itr.first));
                    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[Companion] '%s' learned Artisan of skill %u by itself.", me->GetName(), itr.first);
                }
            }

            void CountAsk(Player* me, Player* leader, State& state, uint32 skill, bool declined, std::vector<std::string> const& tags)
            {
                Record& r = state.records[skill];
                if (!declined)
                    r.asksLeft = r.asksLeft ? uint8(r.asksLeft - 1) : 0;
                else if (!r.declined)
                {
                    r.declined = true;
                    r.asksLeft = 1;
                }
                else
                    r.asksLeft = 0;
                r.state = r.asksLeft ? RecordState::Waiting : RecordState::Exhausted;
                Say(me, r.asksLeft ? (declined ? "artisan.declined" : "") : "artisan.giveup", tags, VarsFor(me, leader, skill));
                Save(me, skill, r);
            }

            // The skill to ask about now, 0 = none.
            uint32 PickSkill(Player const* me, State const& state)
            {
                for (uint32 skill : BotShared::Professions::Primaries())
                {
                    if (!BotShared::Professions::ArtisanAvailable(me, skill) || !CanPay(me, skill))
                        continue;
                    Record const r = RecordOf(state, skill);
                    if (r.state == RecordState::Waiting && r.asksLeft)
                        return skill;
                }
                return 0;
            }

            void Ask(Player* me, Player* leader, State& state, uint32 skill, std::vector<std::string> const& tags)
            {
                Requests::Ask ask;
                ask.intent = Requests::Intent::Artisan;
                ask.text = Life::Phrases::Say("artisan.ask", tags, VarsFor(me, leader, skill));
                ask.options = {
                    { "yes", Life::Phrases::Word("artisan.opt.yes", "yes") },
                    { "no",  Life::Phrases::Word("artisan.opt.no", "no") },
                };
                ask.fallback = "no";    // (an Expired request is told apart from a "no" by its state)
                state.requestId = sRequestBoard.Open(me, leader, ask);
                if (!state.requestId)
                    return;
                state.askingSkill = skill;
                state.askedThisVisit = true;
                BotShared::BotChat::SayToParty(me, ask.text);
            }

            void PollAnswer(Player* me, Player* leader, State& state, std::vector<std::string> const& tags)
            {
                Requests::Status const status = sRequestBoard.Poll(state.requestId);
                if (status.state == Requests::State::Open)
                    return;
                uint32 const skill = state.askingSkill;
                uint32 const id = state.requestId;
                state.requestId = 0;
                state.askingSkill = 0;
                if (status.state == Requests::State::None || !skill)
                {
                    state.askedThisVisit = false;   // dropped: not a real ask
                    return;
                }
                if (status.state == Requests::State::Answered && status.answer == "yes")
                {
                    sRequestBoard.Close(id, Requests::Outcome::Accepted);
                    Record& r = state.records[skill];
                    r.state = RecordState::Active;
                    Save(me, skill, r);
                    state.shownId = 0;
                    state.flagMapId = UINT32_MAX;
                    Say(me, "artisan.accepted", tags, VarsFor(me, leader, skill));
                    return;
                }
                bool const declined = status.state == Requests::State::Answered;
                sRequestBoard.Close(id, declined ? Requests::Outcome::Declined : Requests::Outcome::Expired);
                CountAsk(me, leader, state, skill, declined, tags);
            }

            void Finish(Player* me, Player* leader, State& state, uint32 skill, std::vector<std::string> const& tags)
            {
                if (state.shownId)
                    Mirror::Remove(leader, state.shownId);
                state.shownId = 0;
                Record& r = state.records[skill];
                r.state = RecordState::Done;
                r.helped = true;
                Save(me, skill, r);
                uint32 const now = sCompanionMgr.AddAffinity(me->GetGUIDLow(), AFFINITY, "artisan trip");
                Say(me, "artisan.thanks", tags, VarsFor(me, leader, skill));
                BotShared::BotChat::SystemTo(leader, "PBAFF;" + std::string(me->GetName()) + ";" + std::to_string(now) +
                    ";" + std::to_string(AFFINITY) + ";artisan");
            }

            // The Active trip, leader on the bot's map: the quest in his log, the flag, done.
            void FollowActive(Player* me, Player* leader, State& state, uint32 skill, std::vector<std::string> const& tags)
            {
                if (me->HasSpell(Trainers::RankSpell(skill)))
                {
                    Finish(me, leader, state, skill, tags);
                    return;
                }
                uint32 const questId = Trainers::QuestOf(skill);
                if (state.shownId && !Mirror::IsShown(leader, state.shownId))
                {
                    state.shownId = 0;      // the leader abandoned it: a "no"
                    CountAsk(me, leader, state, skill, true, tags);
                    return;
                }
                if (!state.shownId)
                {
                    switch (Mirror::Show(leader, questId))
                    {
                        case Mirror::ShowResult::Shown:
                            ChatHandler(leader).PSendSysMessage("PBQUEST;%s;mirrorshown;%u", me->GetName(), questId);
                            // (and on)
                        case Mirror::ShowResult::AlreadyShown:
                            state.shownId = questId;
                            state.logFullSaid = false;
                            break;
                        case Mirror::ShowResult::LogFull:
                            if (!state.logFullSaid)
                            {
                                Quest const* quest = sObjectMgr.GetQuestTemplate(questId);
                                ChatHandler(leader).PSendSysMessage("PBQUEST;%s;mirrorfull;%u;%s", me->GetName(), questId,
                                    quest ? quest->GetTitle().c_str() : "");
                            }
                            state.logFullSaid = true;
                            return;
                        case Mirror::ShowResult::NoQuest:
                            return;
                    }
                }
                // The nearest trainer on the leader's continent, once per map.
                if (state.flagMapId != leader->GetMapId())
                {
                    state.flagMapId = leader->GetMapId();
                    std::vector<Trainers::Spawn> const spawns = Trainers::For(me, skill, leader->GetMapId(), leader->GetPositionX(), leader->GetPositionY());
                    if (!spawns.empty() && spawns.front().mapId == leader->GetMapId())
                        Mirror::ShowPoint(leader, spawns.front().x, spawns.front().y, Trainers::NameOf(spawns.front().entry, LocaleIdx(leader)));
                }
            }
        }

        void Update(Player* me, Player* leader, State& state, Life::ArtisanTask& task, std::vector<std::string> const& tags)
        {
            task.wanted.clear();
            if (!me || !leader)
                return;
            if (!state.loaded)
                Load(me, leader, state, tags);

            uint32 const active = ActiveSkill(state);
            if (active)
                task.wanted.insert(active);

            if (state.requestId)
            {
                PollAnswer(me, leader, state, tags);
                return;
            }

            uint32 const now = WorldTimer::getMSTime();
            if (state.lastCheckMs && WorldTimer::getMSTimeDiff(state.lastCheckMs, now) < CHECK_MS)
                return;
            state.lastCheckMs = now;

            // The leader's quest log is the leader's map's: only from the same map.
            bool const sameMap = leader->IsInWorld() && leader->GetMap() == me->GetMap();
            if (active)
            {
                if (sameMap)
                    FollowActive(me, leader, state, active, tags);
                return;
            }

            bool const inCapital = leader->IsInWorld() && InCapital(leader);
            if (inCapital && !state.leaderInCapital)
                state.askedThisVisit = false;   // a new visit
            state.leaderInCapital = inCapital;
            if (!inCapital || state.askedThisVisit || !sameMap)
                return;
            if (me->IsInCombat() || leader->IsInCombat() || !me->IsWithinDistInMap(leader, ASK_RANGE_YD))
                return;
            if (!sRequestBoard.MayAsk(me->GetObjectGuid(), Requests::Intent::Artisan))
                return;
            if (uint32 const skill = PickSkill(me, state))
                Ask(me, leader, state, skill, tags);
        }

        void Stop(State& state)
        {
            if (!state.requestId)
                return;
            sRequestBoard.Close(state.requestId, Requests::Outcome::Cancelled);
            state.requestId = 0;
            state.askingSkill = 0;
            state.askedThisVisit = false;
        }
    }
}
