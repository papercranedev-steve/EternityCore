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
    // 大家等我 / 大家跟我走 / 把掉队的召唤过来: orders for the whole party. Anyone.
    std::unique_ptr<Service> MakeTeamService();
    // 主动开怪 / 被动开怪: the player's pull stance for his party (Commander PullStance.h). Anyone.
    std::unique_ptr<Service> MakePullStanceService();
    // "让我看看你的包": the inspect window with his bags (Gates ManageBags).
    std::unique_ptr<Service> MakeBagsService();
    // His sheet, talents, quest log (addon windows). Anyone.
    std::unique_ptr<Service> MakeLookService();
    // A sub-page (FolderService.cpp): one option `optionWord` (Phrases word) on the first page,
    // then the page `pageLine` (his line) with `services`' options and a way back.
    std::unique_ptr<Service> MakeFolderService(char const* key, char const* optionWord, char const* pageLine,
                                               std::vector<std::unique_ptr<Service>> services);
    // Every party bot of `player`'s group waits / follows; the ones it changed (TeamService,
    // `.partybot stay|follow`).
    std::vector<Player*> OrderTeam(Player* player, bool stay);
    // "能帮我配点药水吗？" per crafting profession: his craft window (Gates Crafting).
    std::unique_ptr<Service> MakeCraftService();
    // "能帮我开个传送门吗？": a mage's portals, a page of cities (Gates MagePortal).
    std::unique_ptr<Service> MakePortalService();
    // `skill` is one of the professions he makes things of for the player (the craft window's).
    bool CraftsFor(Player const* bot, uint32 skill);
}
}

#endif
