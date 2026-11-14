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

            // Nature's Swiftness: the next nature spell with a cast time is instant.
            std::vector<HealBoostSpell> HealBoosts() const override
            {
                HealBoostSpell b;
                b.spell = m_bot.m_spells.shaman.pNaturesSwiftness;
                b.instant = true;
                return { b };
            }

            // Elemental Mastery (talent, 3 min): the next spell crits and costs no mana, used for
            // Chain Lightning (Lightning Bolt until learned).
            std::vector<BurstSpell> Bursts() const override
            {
                auto const& s = m_bot.m_spells.shaman;
                BurstSpell mastery;
                mastery.spell = s.pElementalMastery;
                mastery.then = s.pChainLightning ? s.pChainLightning : s.pLightningBolt;
                mastery.extraSec = 1.0f;
                return { mastery };
            }
        };
    }

    std::unique_ptr<ClassKit> ClassKits::MakeShaman(PartyBotAI const& bot)
    {
        return std::make_unique<ShamanKit>(bot);
    }
}
