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

            // Remove Curse, Abolish Poison (Cure Poison until learned).
            std::vector<SpellEntry const*> Dispels() const override
            {
                auto const& s = m_bot.m_spells.druid;
                return { s.pRemoveCurse, s.pAbolishPoison ? s.pAbolishPoison : s.pCurePoison };
            }

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

            std::vector<SpellEntry const*> FleeStoppers() const override
            {
                return { m_bot.m_spells.druid.pEntanglingRoots };
            }

            // Cat form (CanTryToCastSpell checks the form).
            SpellEntry const* ComboFinisher() const override
            {
                return m_bot.m_spells.druid.pFerociousBite;
            }

            // Cower: cat form only (a bear is a tank, WorldModelBuilder skips tanks; the form is
            // read through PartyBotAI::IsReadyWithStance).
            std::vector<SpellEntry const*> ThreatDrops() const override
            {
                return { m_bot.m_spells.druid.pCower };
            }

            // Hibernate: beasts and dragonkin, caster form.
            std::vector<SpellEntry const*> Controls() const override
            {
                return { m_bot.m_spells.druid.pHibernate };
            }

            // Nature's Swiftness: the next nature spell with a cast time is instant.
            std::vector<HealBoostSpell> HealBoosts() const override
            {
                HealBoostSpell b;
                b.spell = m_bot.m_spells.druid.pNaturesSwiftness;
                b.instant = true;
                return { b };
            }

            // Barkskin (1 min, caster and moonkin form; the form is read through
            // IsReadyWithStance): -20% physical damage taken (the reduction is not in its data).
            // Frenzied Regeneration (bear, 3 min): rage into health over 10 s, about 20% of
            // maximum health (untested guess: the heal is scripted from the rage spent).
            std::vector<SurvivalSpell> Survival() const override
            {
                auto const& s = m_bot.m_spells.druid;
                SurvivalSpell bark;
                bark.spell = s.pBarkskin;
                bark.effect = SurvivalEffect::ReducePhysical;
                bark.fraction = 0.8f;
                bark.selfOnly = true;
                SurvivalSpell regen;
                regen.spell = s.pFrenziedRegeneration;
                regen.effect = SurvivalEffect::ExtraHealth;
                regen.fraction = 0.2f;
                regen.durationSec = 10.0f;
                regen.selfOnly = true;
                return { bark, regen };
            }

            // Innervate (6 min): +400% mana regeneration, all of it while casting, 20 s.
            std::vector<AllyBoostSpell> AllyBoosts() const override
            {
                AllyBoostSpell b;
                b.spell = m_bot.m_spells.druid.pInnervate;
                b.kind = AllyBoostKind::Mana;
                return { b };
            }
        };
    }

    std::unique_ptr<ClassKit> ClassKits::MakeDruid(PartyBotAI const& bot)
    {
        return std::make_unique<DruidKit>(bot);
    }
}
