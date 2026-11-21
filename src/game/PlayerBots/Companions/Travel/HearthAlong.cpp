#include "Companions/Travel/HearthAlong.h"
#include "Companions/Travel/HomeBind.h"
#include "Life/Errands/Travel.h"
#include "Life/Requests/Phrases.h"
#include "Shared/BotChat.h"
#include "Player.h"
#include "Spell.h"
#include "Item.h"
#include "MotionMaster.h"
#include "Log.h"

namespace Companions
{
namespace HearthAlong
{
    bool LeavesByHearth(Player const* leader)
    {
        Spell const* spell = leader->GetCurrentSpell(CURRENT_GENERIC_SPELL);
        return spell && spell->m_CastItem && spell->m_CastItem->GetEntry() == Life::Travel::HEARTHSTONE;
    }

    bool GoesAlong(Player const* me, Player const* leader, bool staying)
    {
        return !staying && me->IsAlive() && me->IsWithinDistInMap(leader, WITH_YD) &&
            !me->IsNonMeleeSpellCasted(false) && Life::Travel::HearthReady(me) &&
            HomeBind::SameBind(me->GetHomeBind(), leader->GetHomeBind());
    }

    void Update(Player* me, Player* leader, State& state, bool staying, std::vector<std::string> const& tags)
    {
        if (state.casting)
        {
            if (!me->IsNonMeleeSpellCasted(false))
            {
                state.casting = false;  // home, or the cast broke
                return;
            }
            // Told to wait here meanwhile, or his cast broke and he is still here: it stays.
            if (staying || (me->IsWithinDistInMap(leader, WITH_YD) && !leader->IsBeingTeleported() && !LeavesByHearth(leader)))
            {
                me->InterruptNonMeleeSpells(false);
                state.casting = false;
            }
            return;
        }
        if (!LeavesByHearth(leader) || !GoesAlong(me, leader, staying))
            return;

        // (a step of the follow would break the cast)
        me->GetMotionMaster()->Clear(false, true);
        me->GetMotionMaster()->MoveIdle();
        if (!Life::Travel::UseHearth(me))
            return;
        state.casting = true;

        Life::Phrases::Vars vars;
        vars["leader"] = leader->GetName();
        vars["home"] = Life::Travel::AreaName(me->GetHomeBindAreaId(), leader);
        std::string const text = Life::Phrases::Say("hearth.along", tags, vars);
        if (!text.empty())
            BotShared::BotChat::SayToParty(me, text);
        sLog.Out(LOG_BASIC, LOG_LVL_DETAIL, "[Companion] %s hearths home with %s (area %u).", me->GetName(),
            leader->GetName(), uint32(me->GetHomeBindAreaId()));
    }
}
}
