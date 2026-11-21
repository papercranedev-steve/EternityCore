#include "Companions/Travel/Departure.h"
#include "Companions/Travel/HearthAlong.h"
#include "Commander/CommanderMgr.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Shared/Phrases.h"
#include "Life/Requests/RequestBoard.h"
#include "Shared/BotChat.h"
#include "PartyBotAI.h"
#include "Group.h"
#include "Player.h"
#include "Spell.h"
#include "SpellEntry.h"

namespace Companions
{
namespace Departure
{
    namespace
    {
        namespace Requests = Life::Requests;

        // A spell that takes him somewhere far: it teleports the caster himself (Hearthstone,
        // Teleport: Stormwind, Astral Recall), being cast now.
        bool LeavesBySpell(Player* leader)
        {
            Spell const* spell = leader->GetCurrentSpell(CURRENT_GENERIC_SPELL);
            SpellEntry const* info = spell ? spell->m_spellInfo : nullptr;
            if (!info)
                return false;
            for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
                if (info->Effect[i] == SPELL_EFFECT_TELEPORT_UNITS && info->EffectImplicitTargetA[i] == TARGET_UNIT_CASTER)
                    return true;
            return false;
        }

        bool Leaving(Player* leader)
        {
            return leader->IsTaxiFlying() || LeavesBySpell(leader);
        }

        std::vector<Player*> PartyBots(Player* leader)
        {
            std::vector<Player*> bots;
            Group* group = leader->GetGroup();
            if (!group)
                return bots;
            for (GroupReference* itr = group->GetFirstMember(); itr; itr = itr->next())
                if (Player* member = itr->getSource())
                    if (member != leader && member->IsInWorld() && member->AI() && dynamic_cast<PartyBotAI*>(member->AI()))
                        bots.push_back(member);
            return bots;
        }

        // The first party bot of the group with him (a flight starts fast: a little more room).
        bool IsAsker(Player* me, Player* leader)
        {
            float const range = leader->IsTaxiFlying() ? NEAR_YD * 2.0f : NEAR_YD;
            for (Player* bot : PartyBots(leader))
                if (bot->IsAlive() && bot->IsWithinDistInMap(leader, range))
                    return bot == me;
            return false;
        }

        bool AnyStays(Player* leader)
        {
            for (Player* bot : PartyBots(leader))
            {
                Commander::StaySpot spot;
                if (sCommanderMgr.GetStay(dynamic_cast<PartyBotAI*>(bot->AI()), spot))
                    return true;
            }
            return false;
        }

        // He hearths and some of the party goes home with him (HearthAlong): nobody asks, the
        // others follow him on their own.
        bool AnyHearthsAlong(Player* leader)
        {
            if (!HearthAlong::LeavesByHearth(leader))
                return false;
            for (Player* bot : PartyBots(leader))
            {
                Commander::StaySpot spot;
                bool const staying = sCommanderMgr.GetStay(dynamic_cast<PartyBotAI*>(bot->AI()), spot);
                if (HearthAlong::LeavesByHearth(bot) || HearthAlong::GoesAlong(bot, leader, staying))
                    return true;
            }
            return false;
        }

        void Say(Player* me, Player* leader, std::vector<std::string> const& tags, char const* key)
        {
            BotShared::Phrases::Vars vars;
            vars["leader"] = leader->GetName();
            BotShared::BotChat::SayToParty(me, BotShared::Phrases::Say(BotShared::I18n::OfParty(me), key, tags, vars));
        }
    }

    void Update(Player* me, Player* leader, State& state, bool staying, std::vector<std::string> const& tags)
    {
        if (state.requestId)
        {
            Requests::Status const status = sRequestBoard.Poll(state.requestId);
            if (status.state == Requests::State::Open)
                return;
            uint32 const id = state.requestId;
            state.requestId = 0;
            if (status.state == Requests::State::None)
                return;
            // (no answer: they wait; running after him across the land is the worse guess)
            if (status.answer == "no")
            {
                sRequestBoard.Close(id, Requests::Outcome::Declined);
                Say(me, leader, tags, "leave.follow");
                return;
            }
            sRequestBoard.Close(id, status.state == Requests::State::Expired ? Requests::Outcome::Expired : Requests::Outcome::Accepted);
            uint32 count = 0;
            for (Player* bot : PartyBots(leader))
                count += sCommanderMgr.SetStay(bot, true) ? 1 : 0;
            if (count)
                Say(me, leader, tags, "talk.stay.team");
            return;
        }

        if (state.asked)
        {
            if (!Leaving(leader) && me->IsWithinDistInMap(leader, NEAR_YD))
                state.asked = false;    // back with the team
            return;
        }
        if (staying || !Leaving(leader) || leader->IsInCombat() || me->IsInCombat() || !IsAsker(me, leader))
            return;
        state.asked = true;
        if (AnyStays(leader) || AnyHearthsAlong(leader) || !sRequestBoard.MayAsk(me->GetObjectGuid(), Requests::Intent::Departure))
            return;

        BotShared::Phrases::Vars vars;
        vars["leader"] = leader->GetName();
        Requests::Ask ask;
        ask.intent = Requests::Intent::Departure;
        ask.text = BotShared::Phrases::Say(BotShared::I18n::OfParty(me), "leave.ask", tags, vars);
        ask.options = {
            { "yes", BotShared::Phrases::Word(BotShared::I18n::OfParty(me), "leave.opt.yes") },
            { "no", BotShared::Phrases::Word(BotShared::I18n::OfParty(me), "leave.opt.no") },
        };
        ask.fallback = "yes";
        state.requestId = sRequestBoard.Open(me, leader, ask);
    }
}
}
