/*
 * Bot daily life (PlayerBots/Life): the business a bot does at NPCs (NpcVisit.h tasks).
 *
 *  - VendorTask: at a vendor sell what only a vendor takes (Shared/BagSorting Fate::Vendor:
 *    gray items, bound gear it replaced; the auction house items wait for a town trip, user
 *    2026-11-23), at an armorer repair
 *    (when it can pay all of it), then buy the missing supplies by priority
 *    (Shared/Supplies, Shared/Vendor::PlanPurchases) keeping the money the class trainer
 *    will want for the important lessons (Trip/LessonPriority.h) for everything but critical
 *    needs.
 *  - TrainerTask: at its class trainer learn what it can pay (Shared/ClassTraining, paid), the
 *    most important first (Trip/LessonPriority.h), the junk only when the money covers all.
 *    TakeLearned tells the bot AI to rebuild its spell data.
 *
 * Map thread of the bot.
 */

#ifndef MANGOS_LIFE_MERCHANT_TASKS_H
#define MANGOS_LIFE_MERCHANT_TASKS_H

#include "Life/Errands/NpcVisit.h"
#include "Life/Errands/Trip/LessonPriority.h"
#include "Shared/BagSorting.h"
#include <set>
#include <vector>

class Item;

namespace Life
{
    class VendorTask final : public INpcTask
    {
    public:
        // `lessons`: as TrainerTask's; the money kept for the trainer is that of the
        // important lessons only (Lessons::IMPORTANT). Null: all of them.
        // `bags`: what the bot sells (Shared/BagSorting), kept up to date by its AI; null = the
        // default rules.
        explicit VendorTask(Lessons::Context const* lessons = nullptr, BotShared::BagSorting::Rules const* bags = nullptr)
            : m_lessons(lessons), m_bags(bags) {}

        char const* Name() const override { return "vendor"; }
        bool Wants(Player* me, Creature* npc) override;
        uint32 Do(Player* me, Creature* npc) override;

    private:
        uint32 TrainingReserve(Player* me) const;
        std::vector<Item*> ToSell(Player* me) const;

        Lessons::Context const* m_lessons;
        BotShared::BagSorting::Rules const* m_bags;
    };

    class TrainerTask final : public INpcTask
    {
    public:
        // `lessons`: what the bot AI knows of itself (Trip/LessonPriority.h), owned by it;
        // null: every lesson is the same (cheapest first, as before).
        explicit TrainerTask(Lessons::Context const* lessons = nullptr) : m_lessons(lessons) {}

        char const* Name() const override { return "trainer"; }
        bool Wants(Player* me, Creature* npc) override;
        // Learns by LessonPriority tier (junk only when the money covers everything).
        uint32 Do(Player* me, Creature* npc) override;
        Lessons::Context const* LessonContext() const { return m_lessons; }

        // Spells were learned since the last call (once).
        bool TakeLearned()
        {
            bool const learned = m_learned;
            m_learned = false;
            return learned;
        }

    private:
        Lessons::Context const* m_lessons;
        bool m_learned = false;
    };

    // At an Artisan trainer the player took it to (Companions/ArtisanTrip): the Artisan rank of
    // the professions the trip is for, paid (Shared/Professions::LearnArtisanAt). The trip sets
    // `wanted` every tick; empty: no business with any trainer.
    class ArtisanTask final : public INpcTask
    {
    public:
        char const* Name() const override { return "artisan trainer"; }
        bool Wants(Player* me, Creature* npc) override;
        uint32 Do(Player* me, Creature* npc) override;

        std::set<uint32> wanted;    // skill lines
    };
}

#endif
