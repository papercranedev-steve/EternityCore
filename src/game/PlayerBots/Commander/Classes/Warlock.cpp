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
        };
    }

    std::unique_ptr<ClassKit> ClassKits::MakeWarlock(PartyBotAI const& bot)
    {
        return std::make_unique<WarlockKit>(bot);
    }
}
