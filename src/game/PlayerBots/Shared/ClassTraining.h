/*
 * Class trainer lessons for bots, by the trainer's own rules (level, previous rank, skill), the
 * same check the trainer window uses (Player::GetTrainerSpellState). Only class trainers of the
 * bot's class: no professions, no weapon masters, no quest rewards (pets, class quest spells).
 *
 * Used when a roster member is recruited (free: what it learned before it met the player) and
 * later by the trainer errand (paid, stage S4). Design: .kiro/docs/2026-11-17-soul-companions.md.
 */

#ifndef MANGOS_BOTSHARED_CLASS_TRAINING_H
#define MANGOS_BOTSHARED_CLASS_TRAINING_H

#include "Common.h"
#include <functional>
#include <vector>

class Player;

namespace BotShared
{
    namespace ClassTraining
    {
        struct Result
        {
            uint32 learned = 0;     // spells learned
            uint32 cost = 0;        // copper paid (0 when free)
            uint32 tooExpensive = 0;    // lessons skipped for lack of money (paid only)
        };

        // Learns every class trainer spell the player may learn now, also the ranks that become
        // available on the way. pay: costs are taken from the player's money and a lesson it cannot
        // afford is skipped (cheapest first, so money goes as far as it reaches).
        // maxLevel: lessons for a higher level are left (0 = the player's level decides alone);
        // a recruit learned less than its level allows (user 2026-11-21).
        Result LearnAvailable(Player* player, bool pay, uint32 maxLevel = 0);

        // Forgets the class trainer spells it knows for a level above maxLevel (a lower rank
        // takes over); a population bot may have learned all of its level. Returns the count.
        uint32 ForgetAbove(Player* player, uint32 maxLevel);

        // What LearnAvailable(player, true) would cost now, first ranks only (no new ranks unlock).
        uint32 CostOfAvailable(Player const* player);

        // The cheapest lesson available now; false when there is none.
        bool CheapestAvailable(Player const* player, uint32& cost);

        // A lesson the trainer would give now (first ranks only: none of them unlocks yet).
        struct Lesson
        {
            uint32 spellId = 0;     // the spell it teaches
            uint32 cost = 0;        // copper
        };
        // Cheapest first.
        std::vector<Lesson> Available(Player const* player);

        // Learns, paying, in the caller's order: `rank` of the taught spell, lower first (the
        // cheaper of equal ranks first). While the money does not cover every lesson available,
        // lessons ranked `skipFrom` or higher are left (user 2026-11-20: no junk when money is
        // short). New ranks that become available on the way are ranked too.
        Result LearnInOrder(Player* player, std::function<uint32(uint32 spellId)> const& rank, uint32 skipFrom);
    }
}

#endif
