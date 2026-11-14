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

            std::vector<SpellEntry const*> FleeStoppers() const override
            {
                return { m_bot.m_spells.paladin.pHammerOfJustice };
            }

            // 6 s stun: holds a chaser for its whole duration.
            std::vector<SpellEntry const*> HoldSpells() const override
            {
                return { m_bot.m_spells.paladin.pHammerOfJustice };
            }

            // Forbearance (Divine Shield, Blessing of Protection) is the spells' mechanic.
            std::vector<SurvivalSpell> Survival() const override
            {
                auto const& s = m_bot.m_spells.paladin;
                SurvivalSpell shield;
                shield.spell = s.pDivineShield;
                shield.effect = SurvivalEffect::Immune;
                shield.selfOnly = true;
                shield.notOnTank = true;
                SurvivalSpell bop;
                bop.spell = s.pBlessingOfProtection;
                bop.effect = SurvivalEffect::ImmunePhysical;
                bop.notOnTank = true;
                bop.targetStops = true;
                SurvivalSpell loh;
                loh.spell = s.pLayOnHands;
                loh.effect = SurvivalEffect::Heal;
                loh.allMana = true;
                // Divine Protection: immune 6 / 8 s, Forbearance like Divine Shield (cooldown doc).
                SurvivalSpell protection;
                protection.spell = s.pDivineProtection;
                protection.effect = SurvivalEffect::Immune;
                protection.selfOnly = true;
                protection.notOnTank = true;
                return { shield, bop, loh, protection };
            }

            // Divine Favor: the next heal crits (spell 20216).
            std::vector<HealBoostSpell> HealBoosts() const override
            {
                HealBoostSpell b;
                b.spell = m_bot.m_spells.paladin.pDivineFavor;
                b.critChance = 1.0f;
                return { b };
            }
        };
    }

    std::unique_ptr<ClassKit> ClassKits::MakePaladin(PartyBotAI const& bot)
    {
        return std::make_unique<PaladinKit>(bot);
    }
}
