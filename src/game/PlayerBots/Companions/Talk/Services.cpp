#include "Companions/Talk/TalkService.h"
#include "Companions/Talk/Services/ServiceList.h"

namespace Companions
{
namespace Talk
{
    std::vector<std::unique_ptr<Service>> const& Services()
    {
        // The window lists them in this order.
        static std::vector<std::unique_ptr<Service>> const services = []()
        {
            std::vector<std::unique_ptr<Service>> list;
            list.push_back(MakeBondService());
            list.push_back(MakeAccountService());
            list.push_back(MakePortalService());
            list.push_back(MakeCraftService());
            // (the unit menu's 互动 and 队伍 sections, user 2026-11-22)
            std::vector<std::unique_ptr<Service>> interact;
            interact.push_back(MakeLookService());
            interact.push_back(MakeBagsService());
            list.push_back(MakeFolderService("interact", "talk.opt.interact", "talk.interact.page", std::move(interact)));
            std::vector<std::unique_ptr<Service>> party;
            party.push_back(MakeStayService());
            party.push_back(MakeTeamService());
            list.push_back(MakeFolderService("party", "talk.opt.party", "talk.party.page", std::move(party)));
            return list;
        }();
        return services;
    }
}
}
