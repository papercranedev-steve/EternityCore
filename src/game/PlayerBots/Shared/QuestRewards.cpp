#include "Shared/QuestRewards.h"
#include "Shared/GearUpgrade.h"
#include "Player.h"
#include "QuestDef.h"
#include "ObjectMgr.h"
#include <algorithm>

namespace BotShared
{
namespace QuestRewards
{
    uint32 BestChoice(Player const* player, Quest const* quest)
    {
        if (!quest || quest->GetRewChoiceItemsCount() <= 1)
            return 0;

        uint32 best = 0;
        float bestGain = 0.0f;
        uint32 bestPrice = 0;
        for (uint32 i = 0; i < QUEST_REWARD_CHOICES_COUNT; ++i)
        {
            ItemPrototype const* proto = sObjectMgr.GetItemPrototype(quest->RewChoiceItemId[i]);
            if (!proto)
                continue;
            // (the suffix of a reward is rolled when it is given)
            float const gain = GearUpgrade::UpgradeGain(player, proto, GearUpgrade::UNKNOWN_SUFFIX);
            uint32 const price = proto->SellPrice * std::max<uint32>(quest->RewChoiceItemCount[i], 1);
            // An upgrade beats any price; between two upgrades (or none) the bigger one, then the price.
            if (gain > bestGain || (gain == bestGain && price > bestPrice))
            {
                best = i;
                bestGain = gain;
                bestPrice = price;
            }
        }
        return best;
    }
}
}
