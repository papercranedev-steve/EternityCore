#include "Companions/Affinity/AffinitySources.h"
#include "Companions/CompanionMgr.h"
#include "Shared/BotChat.h"
#include "Player.h"
#include "Group.h"
#include <algorithm>
#include <string>

namespace Companions
{
    namespace AffinitySources
    {
        namespace
        {
            // Its party has a real player in it: the leader it follows.
            bool WithPlayer(Player* me, Player* leader)
            {
                return leader && !leader->IsBot() && me->GetGroup() && me->GetGroup() == leader->GetGroup();
            }

            void Gain(Player* me, Player* leader, uint32 points, char const* reason)
            {
                uint32 const now = sCompanionMgr.AddAffinity(me->GetGUIDLow(), points, reason);
                if (leader)
                    BotShared::BotChat::SystemTo(leader, "PBAFF;" + std::string(me->GetName()) + ";" + std::to_string(now) +
                        ";" + std::to_string(points) + ";" + reason);
            }
        }

        uint32 StartAt(uint32 level)
        {
            return std::min(level, START_CAP);
        }

        void Update(Player* me, Player* leader, State& state, uint32 paidCopper)
        {
            uint32 const level = me->GetLevel();
            uint32 const before = state.lastLevel ? state.lastLevel : level;
            state.lastLevel = level;
            uint32 const guid = me->GetGUIDLow();
            if (!sCompanionMgr.IsCompanion(guid))
                return;

            if (level > before && WithPlayer(me, leader))
                Gain(me, leader, (level - before) * PER_LEVEL, "levelup");

            if (paidCopper && sCompanionMgr.CountMoneyGift(guid, MAX_MONEY_GIFTS))
                Gain(me, leader, MONEY_GIFT, "gift");
        }
    }
}
