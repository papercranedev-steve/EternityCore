#include "Commander/Classes/ClassKit.h"
#include "PartyBotAI.h"

namespace Commander
{
    namespace
    {
        class ShamanKit : public ClassKit
        {
        public:
            using ClassKit::ClassKit;

            std::vector<SpellEntry const*> Interrupts() const override
            {
                return { m_bot.m_spells.shaman.pEarthShock };
            }

            std::vector<SpellEntry const*> ReflexInterrupts() const override
            {
                return { m_bot.m_spells.shaman.pEarthShock };
            }

            std::vector<SpellEntry const*> Dispels() const override
            {
                auto const& s = m_bot.m_spells.shaman;
                return { s.pCureDisease, s.pCurePoison };
            }

            std::vector<SpellEntry const*> FleeStoppers() const override
            {
                return { m_bot.m_spells.shaman.pFrostShock };
            }
        };
    }

    std::unique_ptr<ClassKit> ClassKits::MakeShaman(PartyBotAI const& bot)
    {
        return std::make_unique<ShamanKit>(bot);
    }
}
