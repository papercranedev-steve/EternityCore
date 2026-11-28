#include "Commander/Classes/ClassKit.h"
#include "TeamBotAI.h"

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

            std::vector<SpellEntry const*> Dispels() const override
            {
                auto const& s = m_bot.m_spells.shaman;
                return { s.pCureDisease, s.pCurePoison };
            }

            std::vector<SpellEntry const*> Purges() const override
            {
                return { m_bot.m_spells.shaman.pPurge };
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

            std::vector<SpellEntry const*> Totems() const override
            {
                auto const& s = m_bot.m_spells.shaman;
                return { s.pManaSpringTotem, s.pHealingStreamTotem, s.pStrengthOfEarthTotem, s.pStoneskinTotem,
                         s.pWindfuryTotem, s.pGraceOfAirTotem, s.pTranquilAirTotem, s.pFlametongueTotem,
                         s.pTremorTotem };
            }

            // Mana Tide Totem (talent, 5 min): every party member within 20 yd gets mana 4 times in
            // 12 s (the totem's spell 16191 / 17355 / 17360: 170 / 230 / 290 each time).
            std::vector<AllyBoostSpell> AllyBoosts() const override
            {
                SpellEntry const* totem = m_bot.m_spells.shaman.pManaTideTotem;
                AllyBoostSpell b;
                b.spell = totem;
                b.kind = AllyBoostKind::PartyMana;
                b.radius = 20.0f;
                if (totem)
                    b.manaPerTarget = 4.0f * (totem->Id == 17359 ? 290.0f : totem->Id == 17354 ? 230.0f : 170.0f);
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

    std::unique_ptr<ClassKit> ClassKits::MakeShaman(TeamBotAI const& bot)
    {
        return std::make_unique<ShamanKit>(bot);
    }
}
