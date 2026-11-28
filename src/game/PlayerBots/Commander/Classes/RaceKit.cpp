#include "Commander/Classes/RaceKit.h"
#include "TeamBotAI.h"
#include "SpellMgr.h"

namespace Commander
{
    namespace
    {
        // Berserking has one spell per power type (mana 20554, rage 26296, energy 26297).
        uint32 const BERSERKING[] = { 20554, 26296, 26297 };
        uint32 const BLOOD_FURY = 20572;
        uint32 const STONEFORM = 20594;
        uint32 const WILL_OF_THE_FORSAKEN = 7744;
        uint32 const FEAR_WARD = 6346;

        SpellEntry const* Known(TeamBotAI const& bot, uint32 spellId)
        {
            return bot.me->HasSpell(spellId) ? sSpellMgr.GetSpellEntry(spellId) : nullptr;
        }
    }

    std::vector<BurstSpell> RaceKit::Bursts(TeamBotAI const& bot)
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

    SpellEntry const* RaceKit::FearBreak(TeamBotAI const& bot)
    {
        return Known(bot, WILL_OF_THE_FORSAKEN);
    }

    SpellEntry const* RaceKit::Stoneform(TeamBotAI const& bot)
    {
        return Known(bot, STONEFORM);
    }

    SpellEntry const* RaceKit::FearWard(TeamBotAI const& bot)
    {
        return Known(bot, FEAR_WARD);
    }

    std::vector<SurvivalSpell> RaceKit::Survival(TeamBotAI const& bot)
    {
        std::vector<SurvivalSpell> out;
        if (SpellEntry const* spell = Stoneform(bot))
        {
            SurvivalSpell s;
            s.spell = spell;
            s.effect = SurvivalEffect::ReducePhysical;
            s.fraction = 0.9f;
            s.selfOnly = true;
            out.push_back(s);
        }
        return out;
    }
}
