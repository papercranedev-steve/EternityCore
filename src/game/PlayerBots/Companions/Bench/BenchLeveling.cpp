#include "Companions/Bench/BenchLeveling.h"
#include "Player.h"
#include "ObjectMgr.h"
#include "World.h"
#include <algorithm>

namespace Companions
{
    namespace BenchLeveling
    {
        uint32 TotalXp(Player const* player)
        {
            uint32 total = player->GetUInt32Value(PLAYER_XP);
            for (uint32 level = 1; level < player->GetLevel(); ++level)
                total += sObjectMgr.GetXPForLevel(level);
            return total;
        }

        uint32 CapFor(uint32 ownerLevel)
        {
            uint32 const cap = ownerLevel > LEVELS_BEHIND ? ownerLevel - LEVELS_BEHIND : 1;
            return std::min(cap, sWorld.getConfig(CONFIG_UINT32_MAX_PLAYER_LEVEL));
        }

        Progress Grow(Progress from, uint32 gain, uint32 cap)
        {
            Progress p = from;
            if (p.level >= cap)
                return p;
            while (gain && p.level < cap)
            {
                uint32 const need = sObjectMgr.GetXPForLevel(p.level);
                uint32 const left = need > p.xp ? need - p.xp : 0;
                if (gain < left)
                {
                    p.xp += gain;
                    break;
                }
                gain -= left;
                ++p.level;
                p.xp = 0;
            }
            if (p.level >= cap)
                p.xp = 0;
            return p;
        }
    }
}
