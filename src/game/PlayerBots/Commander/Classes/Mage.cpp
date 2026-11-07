#include "Commander/Classes/ClassKit.h"
#include "PartyBotAI.h"

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
                return { { s.pBlizzard, true }, { s.pArcaneExplosion, false }, { s.pConeofCold, false },
                         { s.pBlastWave, false } };
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
        };
    }

    std::unique_ptr<ClassKit> ClassKits::MakeMage(PartyBotAI const& bot)
    {
        return std::make_unique<MageKit>(bot);
    }
}
