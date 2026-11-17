#include "Population/Growth.h"
#include "Population/Roster.h"
#include <algorithm>
#include <cmath>

namespace Population
{
    namespace Growth
    {
        uint8 Level(RosterMember const& member, uint8 anchorLevel, uint8 leadCap)
        {
            int32 const cap = std::min<int32>(int32(std::max<uint8>(anchorLevel, 1)) + leadCap, 60);

            int32 level;
            if (member.stallLevel)
                level = member.stallLevel;
            else
                level = int32(std::lround(float(anchorLevel) * member.pace)) + member.jitter;

            return uint8(std::min(std::max(level, 1), cap));
        }

        bool IsRetired(RosterMember const& member, uint8 anchorLevel)
        {
            return member.quitLevel && anchorLevel > member.quitLevel;
        }
    }
}
