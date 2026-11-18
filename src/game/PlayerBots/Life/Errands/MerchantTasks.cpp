#include "Life/Errands/MerchantTasks.h"
#include "Shared/ClassTraining.h"
#include "Shared/Supplies.h"
#include "Shared/Vendor.h"
#include "Shared/Inventory.h"
#include "Player.h"
#include "Item.h"
#include "Creature.h"
#include "Log.h"

namespace Life
{
    namespace
    {
        bool CanRepairAll(Player* me, Creature* npc, uint32& cost)
        {
            if (!npc->HasFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_REPAIR))
                return false;
            cost = BotShared::Vendor::RepairCost(me, npc);
            // All of it or nothing (DurabilityRepairAll repairs item by item while money lasts).
            return cost && me->GetMoney() >= cost;
        }

        // Money the class trainer will want: kept from everything but critical supplies.
        uint32 TrainingReserve(Player* me)
        {
            return BotShared::ClassTraining::CostOfAvailable(me);
        }
    }

    bool VendorTask::Wants(Player* me, Creature* npc)
    {
        if (!npc->HasFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_VENDOR))
            return false;
        uint32 repairCost = 0;
        if (CanRepairAll(me, npc, repairCost) || BotShared::Vendor::HasJunk(me))
            return true;
        std::vector<BotShared::Supplies::Need> const needs = BotShared::Supplies::Missing(me);
        return !needs.empty() && !BotShared::Vendor::PlanPurchases(me, npc, needs, TrainingReserve(me)).empty();
    }

    void VendorTask::Do(Player* me, Creature* npc)
    {
        uint32 const earned = BotShared::Vendor::SellJunk(me, npc);

        uint32 repairCost = 0;
        uint32 repaired = 0;
        if (CanRepairAll(me, npc, repairCost))
            repaired = BotShared::Vendor::RepairAll(me, npc);

        // Planned after selling and repairing: the money is what is left now.
        std::vector<BotShared::Supplies::Need> const needs = BotShared::Supplies::Missing(me);
        uint32 const spent = BotShared::Vendor::Buy(me, npc,
            BotShared::Vendor::PlanPurchases(me, npc, needs, TrainingReserve(me)));

        sLog.Out(LOG_BASIC, LOG_LVL_DETAIL, "[Life] %s at vendor %s: sold for %u, repaired for %u, bought for %u copper.",
            me->GetName(), npc->GetName(), earned, repaired, spent);
    }

    bool VendorTask::WorthTrip(Player* me)
    {
        for (BotShared::Supplies::Need const& need : BotShared::Supplies::Missing(me))
            if (need.priority == BotShared::Supplies::Priority::Critical)
                return true;

        for (uint8 i = EQUIPMENT_SLOT_START; i < EQUIPMENT_SLOT_END; ++i)
            if (Item* item = me->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
            {
                uint32 const maxDurability = item->GetUInt32Value(ITEM_FIELD_MAXDURABILITY);
                if (maxDurability && item->GetUInt32Value(ITEM_FIELD_DURABILITY) * 100 <= maxDurability * TRIP_DURABILITY_PCT)
                    return true;
            }

        return BotShared::Inventory::FreeBagSlots(me) <= BotShared::Vendor::KEEP_FREE_SLOTS && BotShared::Vendor::HasJunk(me);
    }

    uint32 VendorTask::NpcFlags() const
    {
        return UNIT_NPC_FLAG_VENDOR | UNIT_NPC_FLAG_REPAIR;
    }

    bool TrainerTask::WorthTrip(Player* me)
    {
        uint32 cheapest = 0;
        return BotShared::ClassTraining::CheapestAvailable(me, cheapest) && me->GetMoney() >= cheapest;
    }

    uint32 TrainerTask::NpcFlags() const
    {
        return UNIT_NPC_FLAG_TRAINER;
    }

    bool TrainerTask::Wants(Player* me, Creature* npc)
    {
        if (!npc->HasFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_TRAINER))
            return false;
        CreatureInfo const* info = npc->GetCreatureInfo();
        if (!info || info->trainer_type != TRAINER_TYPE_CLASS || info->trainer_class != me->GetClass())
            return false;
        uint32 cheapest = 0;
        return BotShared::ClassTraining::CheapestAvailable(me, cheapest) && me->GetMoney() >= cheapest;
    }

    void TrainerTask::Do(Player* me, Creature* npc)
    {
        BotShared::ClassTraining::Result const result = BotShared::ClassTraining::LearnAvailable(me, true);
        if (result.learned)
            m_learned = true;
        sLog.Out(LOG_BASIC, LOG_LVL_DETAIL, "[Life] %s at trainer %s: %u spells for %u copper, %u too expensive.",
            me->GetName(), npc->GetName(), result.learned, result.cost, result.tooExpensive);
    }
}
