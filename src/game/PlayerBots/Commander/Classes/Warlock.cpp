#include "Commander/Classes/ClassKit.h"
#include "TeamBotAI.h"
#include "SpellMgr.h"

namespace Commander
{
    namespace
    {
        // Pet abilities (Spell Lock, Seduction) are not here: the pet is not a PartyBot.
        class WarlockKit : public ClassKit
        {
        public:
            using ClassKit::ClassKit;

            // Banish: demons and elementals only. Fear: one target. Howl of Terror: everyone
            // around the warlock. Death Coil: a 3 s fear on one target (and a drain); the
            // crowd control assigner and the escape (an enemy hitting the warlock) pick it, the
            // rotation does not any more (review 2026-11-26: it feared the tank's target off
            // the tank, past the doctrine's no-fear stamp).
            std::vector<SpellEntry const*> Controls() const override
            {
                auto const& s = m_bot.m_spells.warlock;
                return { s.pBanish, s.pFear, s.pHowlofTerror, s.pDeathCoil };
            }

            SpellEntry const* Filler() const override
            {
                return m_bot.m_spells.warlock.pShadowBolt;
            }

            // Rain of Fire: a ground channel like Blizzard, same area phase rules (user
            // 2026-11-08); its mana is cheaper through Life Tap. Hellfire burns the warlock
            // too: not here.
            std::vector<AreaDamageSpell> AreaDamage() const override
            {
                return { { m_bot.m_spells.warlock.pRainOfFire, true, 0.0f } };
            }

            SpellEntry const* HealthToMana() const override
            {
                return m_bot.m_spells.warlock.pLifeTap;
            }

            SpellEntry const* DrainHeal() const override
            {
                return m_bot.m_spells.warlock.pDrainLife;
            }

            // Drain Soul: a Soul Shard when the target dies while it channels (Shadowburn's
            // shards come from here; Shadowburn gives its own back on a kill within 5 s).
            SpellEntry const* DeathHarvest() const override
            {
                return m_bot.m_spells.warlock.pDrainSoul;
            }

            // Phase Shift (Imp, 4511, one rank): it keeps its Blood Pact for the party.
            SpellEntry const* PetStandAside() const override
            {
                return sSpellMgr.GetSpellEntry(SPELL_PHASE_SHIFT);
            }

            // Damage curses, one per warlock chosen for the party (CurseAssigner).
            std::vector<SpellEntry const*> Curses() const override
            {
                auto const& s = m_bot.m_spells.warlock;
                return { s.pCurseoftheElements, s.pCurseofShadow, s.pCurseofRecklessness };
            }

            // No bursts: Amplify Curse paired only with Curse of Agony, which the warlocks do not
            // cast any more (user 2026-11-15).

        private:
            static constexpr uint32 SPELL_PHASE_SHIFT = 4511;
        };
    }

    std::unique_ptr<ClassKit> ClassKits::MakeWarlock(TeamBotAI const& bot)
    {
        return std::make_unique<WarlockKit>(bot);
    }
}
