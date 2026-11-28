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

class TeamBotAI;

namespace Commander
{
    namespace RaceKit
    {
        // Berserking (troll: 10-30% attack and casting speed, more at low health), Blood Fury
        // (orc: +25% base attack power, healing taken halved).
        std::vector<BurstSpell> Bursts(TeamBotAI const& bot);
        // Stoneform (dwarf, 3 min): +10% armor, immune to poison, disease and bleeding.
        std::vector<SurvivalSpell> Survival(TeamBotAI const& bot);
        // Breaks a fear or charm on itself (Will of the Forsaken, 2 min): hands and feet, cast as
        // soon as the bot is feared or charmed (TeamBotAI::BreakControl). Null = none.
        SpellEntry const* FearBreak(TeamBotAI const& bot);
        // Stoneform also removes a poison, disease or bleed on itself: cast as soon as one is
        // on (StoneformBehavior, user 2026-11-15). Null = not a dwarf.
        SpellEntry const* Stoneform(TeamBotAI const& bot);
        // Fear Ward (dwarf priest in 1.12, 30 s cooldown): the target is immune to the next fear
        // for 10 min (one charge). Who gets it: FearWardAssigner (boss doc G8). Null = none.
        SpellEntry const* FearWard(TeamBotAI const& bot);
    }
}

#endif
