#include "Shared/Gaze.h"
#include "Player.h"

namespace BotShared
{
namespace Gaze
{
    bool Sees(Player const* viewer, WorldObject const* target)
    {
        if (!viewer || !target || !viewer->IsInWorld() || viewer->IsBeingTeleported() ||
            !viewer->IsWithinDistInMap(target, SEE_YD))
            return false;
        if (viewer->GetSelectionGuid() == target->GetObjectGuid())
            return true;
        return viewer->HasInArc(target, SEE_ARC) && viewer->IsWithinLOSInMap(target);
    }
}
}
