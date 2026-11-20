/*
 * Bot daily life (PlayerBots/Life), the class trainer: which lessons matter most (user
 * 2026-11-20). The trainer learns them in this order and, while the money does not cover all,
 * leaves the junk (Shared/ClassTraining::LearnInOrder); the town trip question lists them by
 * tier (Trip/TrainingPlanner); the bot reminds the leader of the first three tiers out in the
 * field (Life/Errands/LessonReminder).
 *
 * Tiers, most important first:
 *   TeamSupply  conjured goods the party lives on: food and water, a healthstone, a soulstone.
 *   Resurrect   the first rank of a resurrection.
 *   RoleCore    the role's own: a healer's heals (and resurrection ranks), a tank's taunts and
 *               the spells of its talent tree it uses.
 *   Key         damage of its talent tree (one it uses, or a new one), interrupts, crowd control.
 *   Useful      what it uses otherwise, new damage, heals and buffs for others.
 *   Junk        what its AI never casts.
 * "Uses" comes from the bot AI (Context::usedChains: the spells its rotation and the Commander
 * hold, by the first rank of their chain); the talent tree is the skill line most of its
 * talents belong to.
 *
 * Map thread of the bot.
 */

#ifndef MANGOS_LIFE_TRIP_LESSON_PRIORITY_H
#define MANGOS_LIFE_TRIP_LESSON_PRIORITY_H

#include "Common.h"
#include "SharedDefines.h"
#include <set>
#include <string>
#include <vector>

class Player;

namespace Life
{
namespace Lessons
{
    enum class Tier : uint8
    {
        TeamSupply = 1,
        Resurrect,
        RoleCore,
        Key,
        Useful,
        Junk,
    };

    // Up to this tier: the money kept from shopping for the trainer, "the important lessons"
    // of the money question.
    constexpr Tier IMPORTANT = Tier::Key;
    // Up to this tier: reminded out in the field.
    constexpr Tier REMINDED = Tier::RoleCore;

    // What the bot AI knows about itself (filled by it; plain data).
    struct Context
    {
        bool tanks = false;
        bool heals = false;
        std::set<uint32> usedChains;    // first rank of every spell chain the AI casts
    };

    Tier TierOf(Player const* me, uint32 spellId, Context const& context);

    struct Listed
    {
        uint32 spellId = 0;
        uint32 cost = 0;
        Tier tier = Tier::Junk;
    };
    // The lessons available now (Shared/ClassTraining::Available), most important first.
    std::vector<Listed> Available(Player const* me, Context const& context);
    // Their cost up to `upTo` (inclusive).
    uint32 CostUpTo(std::vector<Listed> const& lessons, Tier upTo);

    // "造水术（等级 3）" in the reader's client language (English when missing).
    std::string NameOf(uint32 spellId, LocaleConstant locale);
}
}

#endif
