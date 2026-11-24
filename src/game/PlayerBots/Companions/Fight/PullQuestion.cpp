#include "Companions/Fight/PullQuestion.h"
#include "Commander/CommanderMgr.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Life/Requests/RequestBoard.h"
#include "Shared/BotChat.h"
#include "Shared/GameNames.h"
#include "Shared/I18n.h"
#include "Shared/Phrases.h"
#include "TeamBotAI.h"
#include "Creature.h"
#include "Map.h"
#include "Player.h"

namespace Companions
{
namespace PullQuestion
{
    namespace
    {
        namespace Requests = Life::Requests;
        namespace Phrases = BotShared::Phrases;

        struct Words
        {
            char const* ask;
            char const* yes;
            char const* no;
        };

        Words WordsOf(Commander::PullAskKind kind)
        {
            switch (kind)
            {
                case Commander::PullAskKind::Boss:    return { "pull.ask.boss", "pull.opt.start", "pull.opt.wait" };
                case Commander::PullAskKind::Hard:    return { "pull.ask.hard", "pull.opt.pull", "pull.opt.leave" };
                default:                              return { "pull.ask.area", "pull.opt.clear", "pull.opt.leave" };
            }
        }

        void Close(State& state, Requests::Outcome outcome)
        {
            if (state.requestId)
                sRequestBoard.Close(state.requestId, outcome);
            state.requestId = 0;
        }
    }

    void Update(TeamBotAI* ai, Player* leader, State& state, std::vector<std::string> const& tags)
    {
        Player* me = ai->me;
        Commander::PullQuestion q;
        bool const open = sCommanderMgr.GetPullQuestion(ai, q) && q.asker == me->GetObjectGuid();

        if (state.requestId)
        {
            // (gone meanwhile: the stance turned passive, the question timed out)
            if (!open || q.id != state.questionId)
            {
                Close(state, Requests::Outcome::Cancelled);
                return;
            }
            Requests::Status const status = sRequestBoard.Poll(state.requestId);
            if (status.state == Requests::State::Open)
                return;
            bool const yes = status.state == Requests::State::Answered && status.answer == "yes";
            Close(state, yes ? Requests::Outcome::Accepted
                             : status.state == Requests::State::Expired ? Requests::Outcome::Expired
                                                                        : Requests::Outcome::Declined);
            sCommanderMgr.AnswerPullQuestion(me, state.questionId, yes);
            return;
        }

        if (!open || q.id == state.questionId || me->IsInCombat() || !leader ||
            !sRequestBoard.MayAsk(me->GetObjectGuid(), Requests::Intent::Pull))
            return;
        state.questionId = q.id;

        Creature const* first = me->GetMap()->GetCreature(q.first);
        BotShared::I18n::Lang const lang = BotShared::I18n::OfParty(me);
        Phrases::Vars vars;
        vars["leader"] = leader->GetName();
        vars["name"] = first ? BotShared::GameNames::CreatureName(first->GetEntry(), leader) : std::string();
        vars["count"] = std::to_string(q.count);
        Words const words = WordsOf(q.kind);

        Requests::Ask ask;
        ask.intent = Requests::Intent::Pull;
        ask.text = Phrases::Say(lang, words.ask, tags, vars);
        ask.options = {
            { "yes", Phrases::Word(lang, words.yes) },
            { "no", Phrases::Word(lang, words.no) },
        };
        ask.fallback = "no";
        state.requestId = sRequestBoard.Open(me, leader, ask);
        if (!state.requestId)
            return;     // (not shown: the brain drops it, asks again later)
        sCommanderMgr.MarkPullQuestionShown(me, q.id);
        BotShared::BotChat::SayToParty(me, ask.text);
    }
}
}
