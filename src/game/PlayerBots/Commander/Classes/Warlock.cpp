#include "Commander/Classes/ClassKit.h"
#include "PartyBotAI.h"

namespace Commander
{
    namespace
    {
        // Pet abilities (Spell Lock, Seduction) are not here: the pet is not a PartyBot.
        class WarlockKit : public ClassKit
        {
        public:
            using ClassKit::ClassKit;

            // Banish: demons and elementals only. Fear: one target. Howl of Terror: everyone
            // around the warlock.
            std::vector<SpellEntry const*> Controls() const override
            {
                auto const& s = m_bot.m_spells.warlock;
                return { s.pBanish, s.pFear, s.pHowlofTerror };
            }

            SpellEntry const* Filler() const override
            {
                return m_bot.m_spells.warlock.pShadowBolt;
            }

            // Rain of Fire: a ground channel like Blizzard, same area phase rules (user
            // 2026-11-08); its mana is cheaper through Life Tap. Hellfire burns the warlock
            // too: not here.
            std::vector<AreaDamageSpell> AreaDamage() const override
            {
                return { { m_bot.m_spells.warlock.pRainOfFire, true, 0.0f } };
            }

            SpellEntry const* HealthToMana() const override
            {
                return m_bot.m_spells.warlock.pLifeTap;
            }

            // Amplify Curse (talent, 3 min): the next Curse of Agony does 50% more, when the
            // warlock's Agony is not up on its victim.
            std::vector<BurstSpell> Bursts() const override
            {
                auto const& s = m_bot.m_spells.warlock;
                BurstSpell amplify;
                amplify.spell = s.pAmplifyCurse;
                amplify.then = s.pCurseofAgony;
                amplify.extraSec = 2.0f;
                amplify.thenNotOnTarget = true;
                return { amplify };
            }
        };
    }

    std::unique_ptr<ClassKit> ClassKits::MakeWarlock(PartyBotAI const& bot)
    {
        return std::make_unique<WarlockKit>(bot);
    }
}
