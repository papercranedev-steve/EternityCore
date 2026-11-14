#include "Commander/Classes/ClassKit.h"
#include "PartyBotAI.h"

namespace Commander
{
    namespace
    {
        class PriestKit : public ClassKit
        {
        public:
            using ClassKit::ClassKit;

            // Silence is a shadow talent: null for most healers.
            std::vector<SpellEntry const*> Interrupts() const override
            {
                return { m_bot.m_spells.priest.pSilence };
            }

            std::vector<SpellEntry const*> ReflexInterrupts() const override
            {
                return { m_bot.m_spells.priest.pSilence };
            }

            // Dispel Magic, Abolish Disease (Cure Disease until learned).
            std::vector<SpellEntry const*> Dispels() const override
            {
                auto const& s = m_bot.m_spells.priest;
                return { s.pDispelMagic, s.pAbolishDisease ? s.pAbolishDisease : s.pCureDisease };
            }

            // Undead only (TargetCreatureType, checked by EscapeBehavior); breaks on damage.
            // Psychic Scream is not a hold spell: feared enemies run into the next pack.
            std::vector<SpellEntry const*> HoldSpells() const override
            {
                return { m_bot.m_spells.priest.pShackleUndead };
            }

            // Shackle Undead: undead only. Psychic Scream: everyone around the priest runs.
            std::vector<SpellEntry const*> Controls() const override
            {
                auto const& s = m_bot.m_spells.priest;
                return { s.pShackleUndead, s.pPsychicScream };
            }

            std::vector<SpellEntry const*> ThreatDrops() const override
            {
                return { m_bot.m_spells.priest.pFade };
            }

            // Weakened Soul is not checked by CanTryToCastSpell: the cast may fail, the caller
            // falls back to its other action.
            SpellEntry const* AllyShield() const override
            {
                return m_bot.m_spells.priest.pPowerWordShield;
            }

            // Power Word: Shield on whoever is about to die (Weakened Soul: the spell's mechanic).
            // Desperate Prayer (human, dwarf): a free instant heal on itself.
            std::vector<SurvivalSpell> Survival() const override
            {
                SurvivalSpell s;
                s.spell = m_bot.m_spells.priest.pPowerWordShield;
                s.effect = SurvivalEffect::Absorb;
                SurvivalSpell prayer;
                prayer.spell = m_bot.m_spells.priest.pDesperatePrayer;
                prayer.effect = SurvivalEffect::Heal;
                prayer.selfOnly = true;
                return { s, prayer };
            }

            // Inner Focus: the next spell costs no mana, +25% crit chance (spell 14751).
            std::vector<HealBoostSpell> HealBoosts() const override
            {
                HealBoostSpell b;
                b.spell = m_bot.m_spells.priest.pInnerFocus;
                b.free = true;
                b.critChance = 0.25f;
                return { b };
            }

            // Power Infusion (talent, 3 min): +20% spell damage and healing for 15 s.
            std::vector<AllyBoostSpell> AllyBoosts() const override
            {
                AllyBoostSpell b;
                b.spell = m_bot.m_spells.priest.pPowerInfusion;
                b.kind = AllyBoostKind::Power;
                return { b };
            }

            // Desperate Prayer: no mana cost, 10 min cooldown (spell_template, 1.12).
            SpellEntry const* FreeSelfHeal() const override
            {
                return m_bot.m_spells.priest.pDesperatePrayer;
            }
        };
    }

    std::unique_ptr<ClassKit> ClassKits::MakePriest(PartyBotAI const& bot)
    {
        return std::make_unique<PriestKit>(bot);
    }
}
