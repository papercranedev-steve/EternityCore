/*
 * Bot daily life (PlayerBots/Life): the business a bot does at NPCs (NpcVisit.h tasks).
 *
 *  - VendorTask: at a vendor sell the junk (Shared/Vendor::SellJunk), at an armorer repair
 *    (when it can pay all of it), then buy the missing supplies by priority
 *    (Shared/Supplies, Shared/Vendor::PlanPurchases) keeping the money the class trainer
 *    will want (Shared/ClassTraining::CostOfAvailable) for everything but critical needs.
 *  - TrainerTask: at its class trainer learn what it can pay (Shared/ClassTraining, paid).
 *    TakeLearned tells the bot AI to rebuild its spell data.
 *
 * Map thread of the bot.
 */

#ifndef MANGOS_LIFE_MERCHANT_TASKS_H
#define MANGOS_LIFE_MERCHANT_TASKS_H

#include "Life/Errands/NpcVisit.h"

namespace Life
{
    class VendorTask final : public INpcTask
    {
    public:
        char const* Name() const override { return "vendor"; }
        bool Wants(Player* me, Creature* npc) override;
        void Do(Player* me, Creature* npc) override;
        // A critical supply is missing, an equipped item is nearly broken (TRIP_DURABILITY_PCT),
        // or the bags are full (KEEP_FREE_SLOTS) with junk to sell.
        bool WorthTrip(Player* me) override;
        uint32 NpcFlags() const override;

        static constexpr uint32 TRIP_DURABILITY_PCT = 25;
    };

    class TrainerTask final : public INpcTask
    {
    public:
        char const* Name() const override { return "trainer"; }
        bool Wants(Player* me, Creature* npc) override;
        void Do(Player* me, Creature* npc) override;
        // A new rank it can pay for.
        bool WorthTrip(Player* me) override;
        uint32 NpcFlags() const override;

        // Spells were learned since the last call (once).
        bool TakeLearned()
        {
            bool const learned = m_learned;
            m_learned = false;
            return learned;
        }

    private:
        bool m_learned = false;
    };
}

#endif
