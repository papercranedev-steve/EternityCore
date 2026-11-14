#include "Commander/Classes/ClassKit.h"
#include "PartyBotAI.h"
#include <algorithm>

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

            std::vector<SpellEntry const*> Dispels() const override
            {
                return { m_bot.m_spells.mage.pRemoveLesserCurse };
            }

            std::vector<SpellEntry const*> HoldSpells() const override
            {
                return { m_bot.m_spells.mage.pFrostNova };
            }

            // Frost Nova: around the mage, only when the runner is next to it. (Frostbolt slows
            // too, but it is the filler: not held for this.)
            std::vector<SpellEntry const*> FleeStoppers() const override
            {
                return { m_bot.m_spells.mage.pFrostNova };
            }

            // Humanoids and beasts; one of the sheep / pig / turtle variants (ResetSpellData).
            std::vector<SpellEntry const*> Controls() const override
            {
                return { m_bot.m_spells.mage.pPolymorph };
            }

            // The rotation puts Ice Barrier (frost talent, no mana per absorbed point) up when
            // attacked; Mana Shield (2 mana per point) only when EscapeBehavior finds the hits
            // dangerous and nothing else absorbs (Rotations/MageRotation.cpp).
            std::vector<SpellEntry const*> ReactiveShields() const override
            {
                auto const& s = m_bot.m_spells.mage;
                return { s.pIceBarrier, s.pManaShield };
            }

            std::vector<AreaDamageSpell> AreaDamage() const override
            {
                auto const& s = m_bot.m_spells.mage;
                // Improved Blizzard's chill (1.12: 30 / 50 / 65% slower). The talent is a class
                // script override (UnitAuraProcHandler, scripts 836 / 988 / 989), so the slow is
                // not readable from the Blizzard spell itself.
                Player const* me = m_bot.me;
                float const chill = me->HasSpell(12488) ? 0.65f : me->HasSpell(12487) ? 0.5f
                                  : me->HasSpell(11185) ? 0.3f : 0.0f;
                // Cone of Cold is a slow for what runs at the mage (SlowCone; user 2026-11-12,
                // doc .kiro/docs/2026-11-12-mage-hold.md 3.5), not an area damage spell.
                return { { s.pBlizzard, true, chill }, { s.pArcaneExplosion, false }, { s.pBlastWave, false } };
            }

            SpellEntry const* Filler() const override
            {
                // Fireball for a fire build (deep fire talents), else Frostbolt.
                auto const& s = m_bot.m_spells.mage;
                bool const fire = s.pCombustion || s.pPyroblast;
                if (fire && s.pFireball)
                    return s.pFireball;
                return s.pFrostbolt ? s.pFrostbolt : s.pFireball;
            }

            SpellEntry const* SelfManaRestore() const override
            {
                return m_bot.m_spells.mage.pEvocation;
            }

            SpellEntry const* SelfShield() const override
            {
                return m_bot.m_spells.mage.pIceBarrier;
            }

            SpellEntry const* Leap() const override
            {
                return m_bot.m_spells.mage.pBlink;
            }

            SpellEntry const* SlowCone() const override
            {
                return m_bot.m_spells.mage.pConeofCold;
            }

            // Ice Block: immune, but the mage does nothing while it lasts.
            std::vector<SurvivalSpell> Survival() const override
            {
                SurvivalSpell block;
                block.spell = m_bot.m_spells.mage.pIceBlock;
                block.effect = SurvivalEffect::Immune;
                block.selfOnly = true;
                block.casterStops = true;
                return { block };
            }

            // Combustion (3 min): fire crits climb until 3 crits. Arcane Power (3 min): +30%
            // damage, +30% mana cost (above half mana, as in the original rotation). Presence of
            // Mind (3 min): the next cast is instant, used for Pyroblast (6 s cast).
            std::vector<BurstSpell> Bursts() const override
            {
                auto const& s = m_bot.m_spells.mage;
                BurstSpell combustion;
                combustion.spell = s.pCombustion;
                combustion.damageMult = 0.2f;
                BurstSpell power;
                power.spell = s.pArcanePower;
                power.damageMult = 0.3f;
                power.minManaPct = 50.0f;
                BurstSpell mind;
                mind.spell = s.pPresenceOfMind;
                mind.then = s.pPyroblast ? s.pPyroblast : Filler();
                mind.extraSec = mind.then ? std::max(0.0f, mind.then->GetCastTime(m_bot.me) / 1000.0f - 1.5f) : 0.0f;
                return { combustion, power, mind };
            }
        };
    }

    std::unique_ptr<ClassKit> ClassKits::MakeMage(PartyBotAI const& bot)
    {
        return std::make_unique<MageKit>(bot);
    }
}
