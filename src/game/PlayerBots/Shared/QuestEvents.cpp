#include "Shared/QuestEvents.h"
#include "Player.h"
#include <atomic>

namespace BotShared
{
    namespace QuestEvents
    {
        namespace
        {
            std::atomic<AcceptedListener> g_accepted{ nullptr };
            std::atomic<RewardedListener> g_rewarded{ nullptr };
        }

        void SetListeners(AcceptedListener accepted, RewardedListener rewarded)
        {
            g_accepted.store(accepted);
            g_rewarded.store(rewarded);
        }

        void OnAccepted(Player* player, Quest const* quest, Object* giver)
        {
            if (!player || !quest || player->IsBot())
                return;
            if (AcceptedListener const listener = g_accepted.load())
                listener(player, quest, giver);
        }

        void OnRewarded(Player* player, Quest const* quest)
        {
            if (!player || !quest || player->IsBot())
                return;
            if (RewardedListener const listener = g_rewarded.load())
                listener(player, quest);
        }
    }
}
