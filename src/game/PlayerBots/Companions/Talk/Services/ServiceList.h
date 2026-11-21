/*
 * Companions, talk services (TalkService.h): one factory per service, listed in Services.cpp.
 */

#ifndef MANGOS_COMPANIONS_TALK_SERVICE_LIST_H
#define MANGOS_COMPANIONS_TALK_SERVICE_LIST_H

#include "Companions/Talk/TalkService.h"

namespace Companions
{
namespace Talk
{
    // "以后跟我一起走吧？": a trial partner ready to stay (Recruit/Trial); his yes promotes him.
    std::unique_ptr<Service> MakeBondService();
    // "到我的账号里来吧？": a companion at affinity 100 joins the player's account (Account/JoinOffer).
    std::unique_ptr<Service> MakeAccountService();
    // "在这儿等我" / "跟我走" (Commander/Coordination/Situation/Stay.h). Anyone.
    std::unique_ptr<Service> MakeStayService();
    // "让我看看你的包": the inspect window with his bags (Gates ManageBags).
    std::unique_ptr<Service> MakeBagsService();
    // "能帮我配点药水吗？" per crafting profession: his craft window (Gates Crafting).
    std::unique_ptr<Service> MakeCraftService();
    // "能帮我开个传送门吗？": a mage's portals, a page of cities (Gates MagePortal).
    std::unique_ptr<Service> MakePortalService();
    // `skill` is one of the professions he makes things of for the player (the craft window's).
    bool CraftsFor(Player const* bot, uint32 skill);
}
}

#endif
