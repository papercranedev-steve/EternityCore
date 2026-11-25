#include "Commander/Classes/ClassKit.h"
#include "TeamBotAI.h"
#include "SpellEntry.h"

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

            // Demoralizing Roar: (Dire) Bear Form (CanTryToCastSpell checks the form), 10 rage, 30 s.
            std::vector<SpellEntry const*> AttackPowerDebuffs() const override
            {
                return { m_bot.m_spells.druid.pDemoralizingRoar };
            }

            // Caster form only (CanTryToCastSpell checks the form); 1.5 s cast.
            std::vector<SpellEntry const*> HoldSpells() const override
            {
                return { m_bot.m_spells.druid.pEntanglingRoots };
            }

            // Dash (cat form, the facts check the form): to catch a runner it chases (user
            // 2026-11-15), as the rogue's Sprint.
            std::vector<SpellEntry const*> FleeStoppers() const override
            {
                return { m_bot.m_spells.druid.pEntanglingRoots, m_bot.m_spells.druid.pDash };
            }

            // Cat form (CanTryToCastSpell checks the form).
            SpellEntry const* ComboFinisher() const override
            {
                return m_bot.m_spells.druid.pFerociousBite;
            }

            // Cower: cat form only (a bear is a tank, WorldModelBuilder skips tanks; the form is
            // read through TeamBotAI::IsReadyWithStance).
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

            // Regrowth: the second emergency heal (user 2026-11-15; Nature's Swiftness and
            // Swiftmend come first): 2 s cast, far more mana per health than Healing Touch.
            HealUse CombatHealUse(SpellEntry const* heal) const override
            {
                return heal->IsFitToFamily<SPELLFAMILY_DRUID, CF_DRUID_REGROWTH>() ? HealUse::EmergencyOnly
                                                                                   : HealUse::Normal;
            }

            // Swiftmend (31-point talent, 15 s): eats the target's Rejuvenation or Regrowth.
            SpellEntry const* HotConsumer() const override
            {
                return m_bot.m_spells.druid.pSwiftmend;
            }

            uint32 HotConsumeTicks(SpellEntry const* hot) const override
            {
                if (hot->IsFitToFamily<SPELLFAMILY_DRUID, CF_DRUID_REGROWTH>())
                    return 6;   // "18 sec of Regrowth"
                if (hot->IsFitToFamily<SPELLFAMILY_DRUID, CF_DRUID_REJUVENATION>())
                    return 4;   // "12 sec of Rejuvenation"
                return 0;
            }

            // Barkskin (1 min, caster and moonkin form; the form is read through
            // IsReadyWithStance): -20% physical damage taken (the reduction is not in its data).
            // Rebirth (30 min, a reagent): out of combat it is also the druid's plain resurrection
            // (CombatBotBaseAI::m_resurrectionSpell).
            SpellEntry const* CombatResurrection() const override
            {
                return m_bot.m_spells.druid.pRebirth;
            }

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

            // A caster damage druid only (bear and cat do not cast it): Wrath.
            SpellEntry const* Filler() const override
            {
                return m_bot.GetRole() == ROLE_RANGE_DPS ? m_bot.m_spells.druid.pWrath : nullptr;
            }

            // Hurricane: a ground channel like Blizzard (its slow is on attack speed, not
            // movement); the area phase decides it (user 2026-11-14: the rotation's is gone).
            std::vector<AreaDamageSpell> AreaDamage() const override
            {
                if (m_bot.GetRole() != ROLE_RANGE_DPS)
                    return {};
                return { { m_bot.m_spells.druid.pHurricane, true, 0.0f } };
            }

            // Innervate (6 min): +400% mana regeneration, all of it while casting, 20 s.
            std::vector<AllyBoostSpell> AllyBoosts() const override
            {
                AllyBoostSpell b;
                b.spell = m_bot.m_spells.druid.pInnervate;
                b.kind = AllyBoostKind::Mana;
                return { b };
            }

            // Innervate from bear or cat form: a healer running dry (BoostValue counts the time
            // out of the form, FormExitCost). Rebirth later.
            bool LeavesFormFor(SpellEntry const* spell) const override
            {
                return spell && spell == m_bot.m_spells.druid.pInnervate;
            }

            // As DruidRotation::EnterCombatForm: bear for a tank, cat (bear without it) for
            // melee damage. A healer or a caster druid has none to keep.
            SpellEntry const* CombatForm() const override
            {
                auto const& s = m_bot.m_spells.druid;
                CombatBotRoles const role = m_bot.GetRole();
                if (role == ROLE_HEALER || role == ROLE_RANGE_DPS)
                    return nullptr;
                if (m_bot.m_role == ROLE_TANK)
                    return s.pBearForm;
                if (role == ROLE_MELEE_DPS)
                    return s.pCatForm ? s.pCatForm : s.pBearForm;
                return nullptr;
            }
        };
    }

    std::unique_ptr<ClassKit> ClassKits::MakeDruid(TeamBotAI const& bot)
    {
        return std::make_unique<DruidKit>(bot);
    }
}
