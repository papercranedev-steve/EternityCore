#include "Commander/Classes/ClassKit.h"
#include "PartyBotAI.h"

namespace Commander
{
    namespace
    {
        class DruidKit : public ClassKit
        {
        public:
            using ClassKit::ClassKit;

            // Bear form abilities; CanTryToCastSpell refuses them in other forms.
            std::vector<SpellEntry const*> Interrupts() const override
            {
                auto const& s = m_bot.m_spells.druid;
                return { s.pBash, s.pFeralCharge };
            }

            // Remove Curse, Cure / Abolish Poison.
            bool CanDispel() const override { return true; }

            std::vector<AreaThreatSpell> AreaThreat() const override
            {
                auto const& s = m_bot.m_spells.druid;
                return { { s.pSwipe, 0.8f, false, true, true },
                         { s.pDemoralizingRoar, 0.3f, false, false, false },
                         { s.pChallengingRoar, 0.0f, true, false, false } };
            }

            // Caster form only (CanTryToCastSpell checks the form); 1.5 s cast.
            std::vector<SpellEntry const*> HoldSpells() const override
            {
                return { m_bot.m_spells.druid.pEntanglingRoots };
            }
        };
    }

    std::unique_ptr<ClassKit> ClassKits::MakeDruid(PartyBotAI const& bot)
    {
        return std::make_unique<DruidKit>(bot);
    }
}
