#include "Companions/Affinity/AffinitySources.h"
#include "Companions/CompanionMgr.h"
#include "Shared/BotChat.h"
#include "Player.h"
#include "Group.h"
#include "Map.h"
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

        ClearPoints PointsFor(BotShared::DungeonClears::Info const& segment)
        {
            if (segment.segment == BotShared::DungeonClears::Segment::Onyxia)
                return { 3, 2 };
            return segment.raid ? ClearPoints{ 5, 3 } : ClearPoints{ 2, 1 };
        }

        void OnClear(Map* map, BotShared::DungeonClears::Info const& segment)
        {
            if (!map)
                return;
            ClearPoints const points = PointsFor(segment);
            for (auto itr = map->GetPlayers().getFirst(); itr != nullptr; itr = itr->next())
            {
                Player* bot = itr->getSource();
                if (!bot || !bot->IsInWorld() || !sCompanionMgr.IsCompanion(bot->GetGUIDLow()))
                    continue;
                // A real player of its party in the instance too (the one told about it).
                Player* player = nullptr;
                if (Group* group = bot->GetGroup())
                    for (GroupReference* ref = group->GetFirstMember(); ref && !player; ref = ref->next())
                        if (Player* member = ref->getSource())
                            if (!member->IsBot() && member->IsInWorld() && member->GetMap() == map)
                                player = member;
                if (!player)
                    continue;
                uint32 const times = sCompanionMgr.CountClear(bot->GetGUIDLow(), uint8(segment.segment));
                if (times)
                    Gain(bot, player, times == 1 ? points.first : points.later, times == 1 ? "firstclear" : "clear");
            }
        }
    }
}
