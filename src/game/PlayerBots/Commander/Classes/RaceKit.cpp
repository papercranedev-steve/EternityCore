#include "Commander/Classes/RaceKit.h"
#include "PartyBotAI.h"
#include "SpellMgr.h"

namespace Commander
{
    namespace
    {
        // Berserking has one spell per power type (mana 20554, rage 26296, energy 26297).
        uint32 const BERSERKING[] = { 20554, 26296, 26297 };
        uint32 const BLOOD_FURY = 20572;

        SpellEntry const* Known(PartyBotAI const& bot, uint32 spellId)
        {
            return bot.me->HasSpell(spellId) ? sSpellMgr.GetSpellEntry(spellId) : nullptr;
        }
    }

    std::vector<BurstSpell> RaceKit::Bursts(PartyBotAI const& bot)
    {
        std::vector<BurstSpell> out;
        for (uint32 id : BERSERKING)
        {
            if (SpellEntry const* spell = Known(bot, id))
            {
                BurstSpell b;
                b.spell = spell;
                b.damageMult = 0.15f;
                b.durationSec = 10.0f;
                out.push_back(b);
            }
        }
        if (SpellEntry const* spell = Known(bot, BLOOD_FURY))
        {
            BurstSpell b;
            b.spell = spell;
            b.damageMult = 0.15f;
            b.notTank = true;
            b.healTakenCut = true;
            b.durationSec = 15.0f;
            out.push_back(b);
        }
        return out;
    }
}
