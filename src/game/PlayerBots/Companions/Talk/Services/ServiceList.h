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
    // "在这儿等我" / "跟我走" (Commander/Coordination/Situation/Stay.h). Anyone.
    std::unique_ptr<Service> MakeStayService();
    // "让我看看你的包": the inspect window with his bags (Gates ManageBags).
    std::unique_ptr<Service> MakeBagsService();
}
}

#endif
