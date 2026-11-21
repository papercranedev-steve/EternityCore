#include "Companions/Travel/SummonOffer.h"
#include "Companions/Affinity/Gates.h"
#include "Commander/CommanderMgr.h"
#include "Commander/Coordination/Situation/Summon.h"
#include "Life/Requests/Phrases.h"
#include "Life/Requests/RequestBoard.h"
#include "Shared/BotChat.h"
#include "PartyBotAI.h"
#include "Group.h"
#include "Map.h"
#include "Player.h"
#include "Timer.h"

namespace Companions
{
namespace SummonOffer
{
    namespace
    {
        namespace Requests = Life::Requests;

        uint32 constexpr SOUL_SHARD = 6265;
        float constexpr MOVED_YD = 2.0f;
        float constexpr BACK_YD = 50.0f;

        void Say(Player* me, Player* target, std::vector<std::string> const& tags, std::string const& key,
                 Life::Phrases::Vars vars = {})
        {
            vars["leader"] = target ? target->GetName() : "";
            BotShared::BotChat::SayToParty(me, Life::Phrases::Say(key, tags, vars));
        }

        bool Near(Player* warlock, Player* target)
        {
            return target->IsInMap(warlock) && warlock->IsWithinDist(target, FAR_YD);
        }
    }

    char const* KeyOf(Refusal refusal)
    {
        switch (refusal)
        {
            case Refusal::NotLearned: return "notlearned";
            case Refusal::Locked:     return "locked";
            case Refusal::NoShard:    return "noshard";
            case Refusal::NoHelpers:  return "nohelpers";
            case Refusal::Combat:     return "fighting";
            case Refusal::Here:       return "here";
            case Refusal::Near:       return "near";
            case Refusal::Dead:       return "dead";
            case Refusal::Busy:       return "busy";
            case Refusal::None:
            default:                  return "";
        }
    }

    Refusal Check(Player* warlock, Player* target, std::vector<ObjectGuid>& helpers)
    {
        helpers.clear();
        PartyBotAI* ai = warlock->AI() ? dynamic_cast<PartyBotAI*>(warlock->AI()) : nullptr;
        if (!ai || !warlock->HasSpell(Commander::Summon::RITUAL_OF_SUMMONING))
            return Refusal::NotLearned;
        if (!Gates::Allows(warlock->GetGUIDLow(), Gates::Gate::WarlockSummon))
            return Refusal::Locked;
        ObjectGuid w, t;
        if (sCommanderMgr.GetSummonRole(ai, w, t) != Commander::SummonRole::None)
            return Refusal::Busy;
        if (!warlock->IsAlive() || !target || !target->IsAlive())
            return Refusal::Dead;
        if (warlock->GetMap()->IsDungeon() || warlock->GetMap()->IsBattleGround())
            return Refusal::Here;
        if (Near(warlock, target))
            return Refusal::Near;
        if (warlock->IsInCombat() || target->IsInCombat())
            return Refusal::Combat;
        if (!warlock->HasItemCount(SOUL_SHARD, 1))
            return Refusal::NoShard;

        if (Group* group = warlock->GetGroup())
            for (GroupReference* itr = group->GetFirstMember(); itr && helpers.size() < 2; itr = itr->next())
            {
                Player* member = itr->getSource();
                if (!member || member == warlock || member == target || !member->IsInWorld() || !member->IsAlive() ||
                    member->IsInCombat() || !member->AI() || !dynamic_cast<PartyBotAI*>(member->AI()) ||
                    !member->IsInMap(warlock) || !member->IsWithinDist(warlock, HELPER_YD))
                    continue;
                helpers.push_back(member->GetObjectGuid());
            }
        if (helpers.size() < 2)
            return Refusal::NoHelpers;
        return Refusal::None;
    }

    Refusal Begin(Player* warlock, Player* target, std::vector<std::string> const& tags)
    {
        std::vector<ObjectGuid> helpers;
        Refusal refusal = Check(warlock, target, helpers);
        if (refusal == Refusal::None && !sCommanderMgr.StartSummon(warlock, target->GetObjectGuid(), helpers))
            refusal = Refusal::Busy;
        if (refusal != Refusal::None)
        {
            Say(warlock, target, tags, std::string("summon.no.") + KeyOf(refusal));
            return refusal;
        }
        std::string names;
        for (ObjectGuid guid : helpers)
            if (Player* helper = warlock->GetMap()->GetPlayer(guid))
                names += (names.empty() ? "" : Life::Phrases::Word("place.and", "和")) + helper->GetName();
        Say(warlock, target, tags, "summon.start", { { "helpers", names } });
        return Refusal::None;
    }

    void Update(Player* me, Player* leader, State& state, bool staying, std::vector<std::string> const& tags)
    {
        uint32 const now = WorldTimer::getMSTime();
        if (state.requestId)
        {
            Requests::Status const status = sRequestBoard.Poll(state.requestId);
            if (status.state == Requests::State::Open)
                return;
            uint32 const id = state.requestId;
            state.requestId = 0;
            if (status.state == Requests::State::None)
                return;
            if (status.state == Requests::State::Answered && status.answer == "yes")
            {
                sRequestBoard.Close(id, Requests::Outcome::Accepted);
                Begin(me, leader, tags);
                return;
            }
            sRequestBoard.Close(id, status.state == Requests::State::Expired ? Requests::Outcome::Expired : Requests::Outcome::Declined);
            return;
        }

        // Back with the team: the next departure may be asked about again.
        if (leader->IsInMap(me) && me->IsWithinDist(leader, BACK_YD))
        {
            state.asked = false;
            state.settledSinceMs = 0;
            return;
        }

        // He stands still somewhere (done with his business).
        float const dx = leader->GetPositionX() - state.lastX;
        float const dy = leader->GetPositionY() - state.lastY;
        if (!state.settledSinceMs || leader->GetMapId() != state.lastMap || dx * dx + dy * dy > MOVED_YD * MOVED_YD ||
            leader->IsMoving() || leader->IsTaxiFlying() || leader->IsInCombat() || !leader->IsAlive())
        {
            state.settledSinceMs = now;
            state.lastX = leader->GetPositionX();
            state.lastY = leader->GetPositionY();
            state.lastMap = leader->GetMapId();
            return;
        }
        if (state.asked || !staying || WorldTimer::getMSTimeDiff(state.settledSinceMs, now) < SETTLED_MS)
            return;

        std::vector<ObjectGuid> helpers;
        if (Check(me, leader, helpers) != Refusal::None || !sRequestBoard.MayAsk(me->GetObjectGuid(), Requests::Intent::Summon))
            return;
        state.asked = true;
        Life::Phrases::Vars vars;
        vars["leader"] = leader->GetName();
        Requests::Ask ask;
        ask.intent = Requests::Intent::Summon;
        ask.text = Life::Phrases::Say("summon.ask", tags, vars);
        ask.options = {
            { "yes", Life::Phrases::Word("summon.opt.yes", "拉我回去") },
            { "no", Life::Phrases::Word("summon.opt.no", "先不用") },
        };
        ask.fallback = "no";
        state.requestId = sRequestBoard.Open(me, leader, ask);
    }
}
}
