/*
 * PartyBot Commander layer 1 (eyes): how much mana a bot's recent fights cost it (user
 * 2026-11-24: casters sat down to drink after every single open-world creature and used up
 * their drinks; drink when the mana left will not cover the next fight instead of at a fixed
 * share). A fact the bot keeps about itself (TeamBotAI::m_fightManaSpend, updated every bot
 * tick in UpdateBody); RestValue::DrinkStartShare turns it into the share it drinks below.
 *
 * One fight = in combat to out of combat, alive at the end. Its cost: mana when it started
 * minus mana when it ended, as a share of the pool then (net: regeneration and mana potions
 * during the fight count, as they do for what is left). Fights costing less than
 * MIN_SPEND_SHARE are not counted: a brush with combat (a creature hit by someone else, a
 * critter) is no fight for this bot and would pull the average down.
 * Dungeons (and raids) and the open world keep their own fights: the fights differ.
 */

#ifndef MANGOS_COMMANDER_FIGHT_MANA_SPEND_H
#define MANGOS_COMMANDER_FIGHT_MANA_SPEND_H

#include <array>
#include <cstdint>

class Player;

namespace Commander
{
    struct FightManaSpendState
    {
        static constexpr uint32_t KEPT = 5;     // the last fights kept per place

        struct Fights
        {
            std::array<float, KEPT> spent{};    // shares of the pool, a ring
            uint32_t count = 0;                 // fights recorded (may exceed KEPT)
        };

        bool inFight = false;
        bool fightInDungeon = false;
        float startShare = 0.0f;
        Fights openWorld;
        Fights dungeon;
    };

    namespace FightManaSpend
    {
        static constexpr float MIN_SPEND_SHARE = 0.02f;

        // Every bot tick: starts and ends the fight record. Bots without a mana pool: nothing.
        void Update(Player const* me, FightManaSpendState& state);

        // The average cost of the last fights in this kind of place; false while fewer than
        // `minFights` were recorded there.
        bool Average(FightManaSpendState const& state, bool dungeon, uint32_t minFights, float& share);
    }
}

#endif
