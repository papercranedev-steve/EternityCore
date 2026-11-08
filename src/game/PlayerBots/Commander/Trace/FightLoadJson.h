/*
 * FightLoad::Input / Result as JSON for the calculation trace (Trace/Trace.h) and back, so a
 * recorded run can be re-run step by step (dev console fightload(), dev.ps1 review -Trace).
 * Floats are written as floats (shortest text that reads back to the same float): a re-run of
 * a recorded input gives the recorded result exactly while FightLoad::Run is unchanged.
 *
 * Input:  {partyDps, tankHealth, tankMax, foes=[{g, hp, dps, at, same}],
 *          healers=[{mana, max, regen, rates=[{hps, perMana, cast}]}]}
 * Result: {sec, damage, healed, peak, low, manaStart, manaLeft, dies, unfinished, left}
 */

#ifndef MANGOS_COMMANDER_FIGHT_LOAD_JSON_H
#define MANGOS_COMMANDER_FIGHT_LOAD_JSON_H

#include "Commander/Perception/FightLoad.h"
#include <string>

namespace Commander
{
namespace FightLoadJson
{
    std::string InputJson(FightLoad::Input const& in);
    std::string ResultJson(FightLoad::Result const& r);
    // False with `error` set when the text is not an Input written by InputJson.
    bool ParseInput(std::string const& text, FightLoad::Input& out, std::string& error);
    // The steps of FightLoad::Run: [{t, in, heal, tank, mana, hp=[...]}].
    std::string StepsJson(std::vector<FightLoad::Step> const& steps);
}
}

#endif
