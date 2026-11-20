#include "Shared/Capitals.h"
#include "SharedDefines.h"

namespace BotShared
{
namespace Capitals
{
    std::vector<Capital> const& All()
    {
        static std::vector<Capital> const capitals = {
            { 1537, 0, -4928.0f,  -946.0f, 450.0f, ALLIANCE },     // Ironforge
            { 1519, 0, -8829.0f,   625.0f, 550.0f, ALLIANCE },     // Stormwind
            { 1657, 1,  9947.0f,  2482.0f, 450.0f, ALLIANCE },     // Darnassus
            { 1637, 1,  1568.0f, -4405.0f, 550.0f, HORDE },        // Orgrimmar
            { 1497, 0,  1586.0f,   239.0f, 350.0f, HORDE },        // Undercity
            { 1638, 1, -1278.0f,   123.0f, 450.0f, HORDE },        // Thunder Bluff
        };
        return capitals;
    }

    Capital const* OfZone(uint32 zoneId)
    {
        for (Capital const& c : All())
            if (c.zoneId == zoneId)
                return &c;
        return nullptr;
    }
}
}
