/*
 * Shared bot hands (PlayerBots/Shared): dressing a player (bot or not) in random gear.
 *
 * Two ways (design .kiro/docs/2026-11-28-random-gear-profiles.md):
 *  - FillEmptySlots with RandomGearOptions: any item by quality weights, item level cap and the
 *    class's primary stat, uniform among them. Population bots that never joined anyone
 *    (Population/Growth: deliberately poor) and `.partybot add` (PartyBot.AutoEquip = 1). Same
 *    seed, same gear.
 *  - EquipStage with a gear stage (Shared/GearStages): per slot a quality and a source of the
 *    stage, then among the better fitting half (Shared/GearFit) of what is left, none below half
 *    the best fit; random property
 *    items get the suffix the class would keep. Test scenarios and boss tests (DevConsole
 *    u:gear), later player team bots.
 *
 * Rules every way follows: no tools, fishing or arena gear (GearSources::IsOddity), the armor
 * type the class wears best (healers may wear lighter), only known weapon skills, shields only
 * for shield class tanks, holdables only for casters.
 *
 * Map thread of the player.
 */

#ifndef MANGOS_BOTSHARED_RANDOM_GEAR_H
#define MANGOS_BOTSHARED_RANDOM_GEAR_H

#include "Common.h"
#include "SharedDefines.h"
#include "Shared/GearStages.h"

class Player;

// Limits of the FillEmptySlots way. The defaults keep the PartyBot behaviour, except for rules
// every bot follows (no tools, fishing or arena gear; trinkets as below).
struct RandomGearOptions
{
    // Players get their first real trinket late in levelling: below this level the trinket slots
    // stay empty, and even then never a crafted one (engineering gadgets and the like).
    uint32 minTrinketLevel = 55;
    bool allowPvP = true;           // false: no honor rank items, no PvP preference
    bool allowCosmetic = true;      // false: no armor-less clothes in armor slots (dresses, tuxedos)
    uint32 maxItemLevel = 0;        // 0 = no cap
    uint32 qualityWeights[MAX_ITEM_QUALITY] = {};  // per slot, pick a quality first; all 0 = any item
    uint32 skipSlotMask = 0;        // 1 << EQUIPMENT_SLOT_*: leave these empty
    uint32 seed = 0;                // != 0: same seed, same picks; 0 = random
};

namespace BotShared
{
    namespace RandomGear
    {
        struct StageRequest
        {
            GearStages::Stage stage = GearStages::Stage::Leveling;
            uint32 strength = 50;       // 0-100: where between the stage's low and high mix
            uint32 maxItemLevel = 0;    // 0 = the stage's window only
            uint32 skipSlotMask = 0;    // 1 << EQUIPMENT_SLOT_*: leave these empty
            uint32 seed = 0;            // != 0: same seed, same picks; 0 = random
        };

        // Plate (warriors, paladins) and mail (hunters, shamans) from level 40, as the trainer
        // teaches them.
        void LearnArmorProficiencies(Player* player);

        // `role`: CombatBotRoles (ROLE_INVALID: the class's usual one). Empty slots only.
        void FillEmptySlots(Player* player, uint8 role, RandomGearOptions const& options);
        // Also learns dual wield where the class has it at this level. Empty slots only: call
        // StripEquipment first for a new set. Returns how many items were put on.
        uint32 EquipStage(Player* player, uint8 role, StageRequest const& request);

        // Destroys everything worn (not the bags).
        void StripEquipment(Player* player);
    }
}

#endif
