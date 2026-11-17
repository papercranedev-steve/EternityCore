#include "Shared/LootFacts.h"
#include "Creature.h"
#include "GameObject.h"
#include "Map.h"
#include "WorldSession.h"
#include "Group.h"
#include "LootMgr.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "CellImpl.h"
#include <list>

namespace BotShared
{
namespace LootFacts
{
    namespace
    {
        // Corpse with loot left on it (the client shows it as lootable).
        class LootableCorpseCheck
        {
        public:
            LootableCorpseCheck(WorldObject const* center, float range) : m_center(center), m_range(range) {}
            bool operator()(Creature* c)
            {
                return c->IsDead() && c->HasFlag(UNIT_DYNAMIC_FLAGS, UNIT_DYNFLAG_LOOTABLE) &&
                    m_center->IsWithinDist(c, m_range, false);
            }

        private:
            WorldObject const* m_center;
            float m_range;
        };

        // The player's own copy of item `index` (FFA, quest or conditional list) is still on the corpse.
        bool HasUntakenEntry(QuestItemMap const& lists, uint32 playerLow, size_t index)
        {
            QuestItemMap::const_iterator const itr = lists.find(playerLow);
            if (itr == lists.end())
                return false;
            for (QuestItem const& qi : *itr->second)
                if (qi.index == index && !qi.is_looted)
                    return true;
            return false;
        }

        // Shared, FFA or conditional item `i` the player would take now.
        bool TakesItem(Player const* player, Loot const& loot, WorldObject const* target, size_t i, bool includePoor)
        {
            LootItem const& item = loot.items[i];
            if (item.is_looted || item.is_blocked || (!includePoor && !IsWorthLooting(item)))
                return false;
            uint32 const playerLow = player->GetGUIDLow();
            if (item.freeforall)
                return HasUntakenEntry(loot.GetPlayerFFAItems(), playerLow, i);
            if (item.conditionId)
                return HasUntakenEntry(loot.GetPlayerNonQuestNonFFAConditionalItems(), playerLow, i);
            return item.AllowedForPlayer(player, target) && MayTakeSharedItem(player, loot, item);
        }
    }

    bool IsWorthLooting(LootItem const& item)
    {
        ItemPrototype const* proto = sObjectMgr.GetItemPrototype(item.itemid);
        return proto && proto->Quality > ITEM_QUALITY_POOR;
    }

    bool MayTakeSharedItem(Player const* player, Loot const& loot, LootItem const& item)
    {
        if (!loot.roundRobinPlayer || loot.roundRobinPlayer == player->GetGUID())
            return true;
        Group const* group = player->GetGroup();
        if (!group)
            return true;
        switch (group->GetLootMethod())
        {
            case ROUND_ROBIN:
                return false;
            case GROUP_LOOT:
            case NEED_BEFORE_GREED:
                return !item.is_underthreshold;
            default:
                return true;
        }
    }

    bool HasLootWorthTaking(Player const* player, Creature const* corpse, bool includePoor)
    {
        Loot const& loot = corpse->loot;
        if (loot.gold)
            return true;
        return !SlotsToTake(player, corpse, includePoor).empty();
    }

    std::vector<Creature*> LootableCorpsesNear(WorldObject const* center, float radius)
    {
        std::list<Creature*> found;
        LootableCorpseCheck check(center, radius);
        MaNGOS::CreatureListSearcher<LootableCorpseCheck> searcher(found, check);
        Cell::VisitGridObjects(center, searcher, radius);
        return std::vector<Creature*>(found.begin(), found.end());
    }

    std::vector<uint8> SlotsToTake(Player const* player, Creature const* corpse, bool includePoor)
    {
        return SlotsToTake(player, corpse->loot, corpse, includePoor);
    }

    std::vector<uint8> SlotsToTake(Player const* player, Loot const& loot, WorldObject const* target, bool includePoor)
    {
        std::vector<uint8> slots;
        for (size_t i = 0; i < loot.items.size(); ++i)
            if (TakesItem(player, loot, target, i, includePoor))
                slots.push_back(uint8(i));

        // Quest items follow the shared list, in the order of the player's own quest list.
        QuestItemMap::const_iterator const quest = loot.GetPlayerQuestItems().find(player->GetGUIDLow());
        if (quest != loot.GetPlayerQuestItems().end())
        {
            size_t questSlot = 0;
            for (QuestItem const& qi : *quest->second)
            {
                if (!qi.is_looted && qi.index < loot.m_questItems.size() && !loot.m_questItems[qi.index].is_looted)
                    slots.push_back(uint8(loot.items.size() + questSlot));
                ++questSlot;
            }
        }
        return slots;
    }

    bool TakeOpenLoot(Player* player, bool includePoor)
    {
        ObjectGuid const guid = player->GetLootGuid();
        if (guid.IsEmpty())
            return false;

        Loot const* loot = nullptr;
        WorldObject const* target = nullptr;
        if (guid.IsGameObject())
        {
            if (GameObject* go = player->GetMap()->GetGameObject(guid))
            {
                loot = &go->loot;
                target = go;
            }
        }
        else if (guid.IsCreature())
        {
            if (Creature* creature = player->GetMap()->GetCreature(guid))
            {
                loot = &creature->loot;
                target = creature;
            }
        }

        WorldSession* session = player->GetSession();
        if (loot)
        {
            if (loot->gold)
                session->HandleLootMoneyOpcode(NullClientPacket(CMSG_LOOT_MONEY));
            for (uint8 slot : SlotsToTake(player, *loot, target, includePoor))
            {
                WorldPackets::Loot::AutoStoreLootItem packet;
                packet.lootSlot = slot;
                session->HandleAutostoreLootItemOpcode(packet);
            }
        }
        session->DoLootRelease(guid);
        return true;
    }
}
}
