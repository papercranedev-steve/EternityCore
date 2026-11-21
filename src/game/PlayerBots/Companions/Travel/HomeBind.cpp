#include "Companions/Travel/HomeBind.h"
#include "Life/Errands/Travel.h"
#include "Life/Requests/Phrases.h"
#include "Shared/BotChat.h"
#include "Player.h"
#include "Log.h"
#include <cmath>

namespace Companions
{
namespace HomeBind
{
    bool SameBind(WorldLocation const& a, WorldLocation const& b)
    {
        return a.mapId == b.mapId && std::hypot(a.x - b.x, a.y - b.y) <= SAME_YD && std::fabs(a.z - b.z) <= SAME_YD;
    }

    void Update(Player* me, Player* leader, State& state, std::vector<std::string> const& tags)
    {
        WorldLocation const& his = leader->GetHomeBind();
        // (he bound somewhere new: the line in party chat when it saw him do it)
        bool const changed = state.known && !SameBind(state.seen, his);
        state.known = true;
        state.seen = his;
        if (SameBind(me->GetHomeBind(), his))
            return;

        me->SetHomebindToLocation(his, leader->GetHomeBindAreaId());
        sLog.Out(LOG_BASIC, LOG_LVL_DETAIL, "[Companion] %s binds its hearthstone with %s (area %u).", me->GetName(),
            leader->GetName(), uint32(leader->GetHomeBindAreaId()));
        if (!changed || !me->IsInMap(leader) || !me->IsWithinDist(leader, WITH_YD))
            return;
        Life::Phrases::Vars vars;
        vars["leader"] = leader->GetName();
        vars["home"] = Life::Travel::AreaName(leader->GetHomeBindAreaId(), leader);
        std::string const text = Life::Phrases::Say("home.bind", tags, vars);
        if (!text.empty())
            BotShared::BotChat::SayToParty(me, text);
    }
}
}
