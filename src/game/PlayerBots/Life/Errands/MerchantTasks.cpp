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
    }

    // Money the class trainer will want for the important lessons: kept from everything but
    // critical supplies (the junk lessons do not keep food from being bought).
    uint32 VendorTask::TrainingReserve(Player* me) const
    {
        if (!m_lessons)
            return BotShared::ClassTraining::CostOfAvailable(me);
        return Lessons::CostUpTo(Lessons::Available(me, *m_lessons), Lessons::IMPORTANT);
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

    uint32 VendorTask::Do(Player* me, Creature* npc)
    {
        uint32 sold = 0;
        uint32 const earned = BotShared::Vendor::SellJunk(me, npc, &sold);

        uint32 repairCost = 0;
        uint32 repaired = 0;
        if (CanRepairAll(me, npc, repairCost))
            repaired = BotShared::Vendor::RepairAll(me, npc);

        // Planned after selling and repairing: the money is what is left now.
        std::vector<BotShared::Supplies::Need> const needs = BotShared::Supplies::Missing(me);
        std::vector<BotShared::Vendor::Purchase> const purchases =
            BotShared::Vendor::PlanPurchases(me, npc, needs, TrainingReserve(me));
        uint32 const spent = BotShared::Vendor::Buy(me, npc, purchases);

        sLog.Out(LOG_BASIC, LOG_LVL_DETAIL, "[Life] %s at vendor %s: sold for %u, repaired for %u, bought for %u copper.",
            me->GetName(), npc->GetName(), earned, repaired, spent);
        return sold + (repaired ? 1 : 0) + uint32(purchases.size());
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

    uint32 TrainerTask::Do(Player* me, Creature* npc)
    {
        BotShared::ClassTraining::Result result;
        if (m_lessons)
        {
            Lessons::Context const& context = *m_lessons;
            result = BotShared::ClassTraining::LearnInOrder(me, [me, &context](uint32 spellId)
            {
                return uint32(Lessons::TierOf(me, spellId, context));
            }, uint32(Lessons::Tier::Junk));
        }
        else
            result = BotShared::ClassTraining::LearnAvailable(me, true);
        if (result.learned)
            m_learned = true;
        sLog.Out(LOG_BASIC, LOG_LVL_DETAIL, "[Life] %s at trainer %s: %u spells for %u copper, %u too expensive.",
            me->GetName(), npc->GetName(), result.learned, result.cost, result.tooExpensive);
        return result.learned;
    }
}
