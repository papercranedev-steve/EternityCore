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
        Result LearnAvailable(Player* player, bool pay);

        // What LearnAvailable(player, true) would cost now, first ranks only (no new ranks unlock).
        uint32 CostOfAvailable(Player const* player);

        // The cheapest lesson available now; false when there is none.
        bool CheapestAvailable(Player const* player, uint32& cost);
    }
}

#endif
