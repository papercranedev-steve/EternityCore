#include "Commander/Classes/ClassKit.h"
#include "TeamBotAI.h"
#include "SpellEntry.h"

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

            // Holy Shock (user 2026-11-15): instant, 30 s cooldown, little heal per mana: for an
            // emergency the cast-time heals would not reach in time.
            HealUse CombatHealUse(SpellEntry const* heal) const override
            {
                if (heal->IsFitToFamily<SPELLFAMILY_PALADIN, CF_PALADIN_HOLY_SHOCK>())
                    return HealUse::EmergencyOnly;
                return HealUse::Normal;
            }

            // Little spirit regeneration, Illumination refunds crits: casts Flash of Light
            // steadily rather than lift + rest (user 2026-11-16).
            bool HealsSteadily() const override { return true; }

            std::vector<SpellEntry const*> Blessings() const override
            {
                auto const& s = m_bot.m_spells.paladin;
                return { s.pBlessingOfWisdom, s.pBlessingOfMight, s.pBlessingOfKings,
                         s.pBlessingOfSalvation, s.pBlessingOfLight, s.pBlessingOfSanctuary,
                         s.pGreaterBlessingOfWisdom, s.pGreaterBlessingOfMight, s.pGreaterBlessingOfKings,
                         s.pGreaterBlessingOfSalvation, s.pGreaterBlessingOfLight, s.pGreaterBlessingOfSanctuary };
            }

            // Only with Judgement learned (it is what puts the debuff on).
            std::vector<SpellEntry const*> JudgementSeals() const override
            {
                auto const& s = m_bot.m_spells.paladin;
                if (!s.pJudgement)
                    return {};
                return { s.pSealOfWisdom, s.pSealOfLight };
            }

            std::vector<SpellEntry const*> PartyAuras() const override
            {
                auto const& s = m_bot.m_spells.paladin;
                return { s.pDevotionAura, s.pConcentrationAura, s.pRetributionAura, s.pSanctityAura,
                         s.pShadowResistanceAura, s.pFrostResistanceAura, s.pFireResistanceAura };
            }
        };
    }

    std::unique_ptr<ClassKit> ClassKits::MakePaladin(TeamBotAI const& bot)
    {
        return std::make_unique<PaladinKit>(bot);
    }
}
