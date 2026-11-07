#include "Commander/Classes/ClassKit.h"
#include "PartyBotAI.h"

namespace Commander
{
    namespace
    {
        class PaladinKit : public ClassKit
        {
        public:
            using ClassKit::ClassKit;

            std::vector<SpellEntry const*> Interrupts() const override
            {
                return { m_bot.m_spells.paladin.pHammerOfJustice };
            }

            std::vector<SpellEntry const*> ReflexInterrupts() const override
            {
                return { m_bot.m_spells.paladin.pHammerOfJustice };
            }

            // Cleanse (Purify until learned).
            std::vector<SpellEntry const*> Dispels() const override
            {
                auto const& s = m_bot.m_spells.paladin;
                return { s.pCleanse ? s.pCleanse : s.pPurify };
            }

            std::vector<AreaThreatSpell> AreaThreat() const override
            {
                return { { m_bot.m_spells.paladin.pConsecration, 0.5f, false, false, true } };
            }

            // 6 s stun: holds a chaser for its whole duration.
            std::vector<SpellEntry const*> HoldSpells() const override
            {
                return { m_bot.m_spells.paladin.pHammerOfJustice };
            }
        };
    }

    std::unique_ptr<ClassKit> ClassKits::MakePaladin(PartyBotAI const& bot)
    {
        return std::make_unique<PaladinKit>(bot);
    }
}
