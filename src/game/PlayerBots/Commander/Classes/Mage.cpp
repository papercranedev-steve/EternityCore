#include "Commander/Classes/ClassKit.h"
#include "PartyBotAI.h"

namespace Commander
{
    namespace
    {
        class MageKit : public ClassKit
        {
        public:
            using ClassKit::ClassKit;

            std::vector<SpellEntry const*> Interrupts() const override
            {
                return { m_bot.m_spells.mage.pCounterspell };
            }

            std::vector<SpellEntry const*> ReflexInterrupts() const override
            {
                return { m_bot.m_spells.mage.pCounterspell };
            }

            // Remove Lesser Curse.
            bool CanDispel() const override { return true; }

            std::vector<SpellEntry const*> HoldSpells() const override
            {
                return { m_bot.m_spells.mage.pFrostNova };
            }

            // The rotation casts Mana Shield when attackers are near (PartyBotAI mage block,
            // above 20% mana); Ice Barrier is a frost talent.
            std::vector<SpellEntry const*> ReactiveShields() const override
            {
                auto const& s = m_bot.m_spells.mage;
                return { s.pIceBarrier, s.pManaShield };
            }
        };
    }

    std::unique_ptr<ClassKit> ClassKits::MakeMage(PartyBotAI const& bot)
    {
        return std::make_unique<MageKit>(bot);
    }
}
