#include "Commander/Classes/ClassKit.h"
#include "TeamBotAI.h"
#include "SpellEntry.h"

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

            // Dispel Magic, Abolish Disease (Cure Disease until learned).
            std::vector<SpellEntry const*> Dispels() const override
            {
                auto const& s = m_bot.m_spells.priest;
                return { s.pDispelMagic, s.pAbolishDisease ? s.pAbolishDisease : s.pCureDisease };
            }

            std::vector<SpellEntry const*> Purges() const override
            {
                return { m_bot.m_spells.priest.pDispelMagic };
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

            // A damage priest only (a healer's damage is not compared with anything): Mind Flay
            // for a shadow build, else Smite.
            SpellEntry const* Filler() const override
            {
                if (m_bot.GetRole() != ROLE_RANGE_DPS)
                    return nullptr;
                auto const& s = m_bot.m_spells.priest;
                return (s.pShadowform && s.pMindFlay) ? s.pMindFlay : s.pSmite;
            }

            // Holy Nova around the priest (user 2026-11-14: the rotation's "three on me" Holy Nova
            // is gone, the area phase decides); a damage priest only (WorldModelBuilder fills area
            // facts for damage roles; a healer does not cast it).
            std::vector<AreaDamageSpell> AreaDamage() const override
            {
                if (m_bot.GetRole() != ROLE_RANGE_DPS)
                    return {};
                return { { m_bot.m_spells.priest.pHolyNova, false, 0.0f } };
            }

            // Renew: not in combat (user 2026-11-15; Heal ranks do more per mana, and every cast
            // restarts the five second rule).
            HealUse CombatHealUse(SpellEntry const* heal) const override
            {
                return heal->IsFitToFamily<SPELLFAMILY_PRIEST, CF_PRIEST_RENEW>() ? HealUse::Never : HealUse::Normal;
            }

            // Desperate Prayer: no mana cost, 10 min cooldown (spell_template, 1.12).
            SpellEntry const* FreeSelfHeal() const override
            {
                return m_bot.m_spells.priest.pDesperatePrayer;
            }

            // Heals (Heal, Flash Heal, Renew, Desperate Prayer...) are not allowed in Shadowform
            // (spell_template stancesNot 1 << 27; Power Word: Shield and Inner Focus are): a
            // shadow priest that heals for the team leaves the form for the heal the heal module
            // or the team rescue orders (until 2026-11-26 it left the form at the start of every
            // fight once it had the heal function, and never took it again).
            bool LeavesFormFor(SpellEntry const* spell) const override
            {
                return spell && (spell->HasEffect(SPELL_EFFECT_HEAL) || spell->HasAura(SPELL_AURA_PERIODIC_HEAL));
            }

            // Shadowform again at the next fight, not after each heal (ClassKit.h).
            bool TakesFormBackAfterExit() const override
            {
                return false;
            }
        };
    }

    std::unique_ptr<ClassKit> ClassKits::MakePriest(TeamBotAI const& bot)
    {
        return std::make_unique<PriestKit>(bot);
    }
}
