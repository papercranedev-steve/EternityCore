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
        uint32 Do(Player* me, Creature* npc) override;
    };

    class TrainerTask final : public INpcTask
    {
    public:
        char const* Name() const override { return "trainer"; }
        bool Wants(Player* me, Creature* npc) override;
        uint32 Do(Player* me, Creature* npc) override;

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
