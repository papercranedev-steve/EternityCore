/*
 * PartyBot Commander: racial abilities, the same slots as ClassKit for spells that come with the
 * race, not the class (cooldown doc .kiro/docs/2026-11-14-cooldowns.md 3). Looked up by spell id
 * (Player::HasSpell); WorldModelBuilder merges them with the class kit's lists, so the modules
 * do not care where a spell comes from.
 */

#ifndef MANGOS_COMMANDER_RACE_KIT_H
#define MANGOS_COMMANDER_RACE_KIT_H

#include "Commander/Classes/ClassKit.h"
#include <vector>

class PartyBotAI;

namespace Commander
{
    namespace RaceKit
    {
        // Berserking (troll: 10-30% attack and casting speed, more at low health), Blood Fury
        // (orc: +25% base attack power, healing taken halved).
        std::vector<BurstSpell> Bursts(PartyBotAI const& bot);
        // Stoneform (dwarf, 3 min): +10% armor, immune to poison, disease and bleeding.
        std::vector<SurvivalSpell> Survival(PartyBotAI const& bot);
    }
}

#endif
