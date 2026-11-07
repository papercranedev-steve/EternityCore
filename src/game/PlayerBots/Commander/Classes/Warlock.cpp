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
        };
    }

    std::unique_ptr<ClassKit> ClassKits::MakeWarlock(PartyBotAI const& bot)
    {
        return std::make_unique<WarlockKit>(bot);
    }
}
