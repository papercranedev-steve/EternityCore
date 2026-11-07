#include "Commander/Classes/ClassKit.h"
#include "PartyBotAI.h"

namespace Commander
{
    namespace
    {
        class RogueKit : public ClassKit
        {
        public:
            using ClassKit::ClassKit;

            // Gouge is a stun with spell mechanic KNOCKOUT (incapacitate): CastValues::CanStop
            // skips it on knockout-immune creatures (mechanicMask = 1 << 13).
            std::vector<SpellEntry const*> Interrupts() const override
            {
                auto const& s = m_bot.m_spells.rogue;
                return { s.pKick, s.pGouge };
            }

            std::vector<SpellEntry const*> ReflexInterrupts() const override
            {
                auto const& s = m_bot.m_spells.rogue;
                return { s.pGouge, s.pKick };
            }

            // Sap: stealthed, humanoids not in combat. Blind: needs Blinding Powder.
            std::vector<SpellEntry const*> Controls() const override
            {
                auto const& s = m_bot.m_spells.rogue;
                return { s.pSap, s.pBlind };
            }

            // Kidney Shot: a finisher, needs combo points on the target.
            std::vector<SpellEntry const*> FleeStoppers() const override
            {
                return { m_bot.m_spells.rogue.pKidneyShot };
            }

            SpellEntry const* ComboFinisher() const override
            {
                return m_bot.m_spells.rogue.pEviscerate;
            }

            SpellEntry const* Stealth() const override
            {
                return m_bot.m_spells.rogue.pStealth;
            }

            // Vanish also as a way to shake an enemy off (team rescue "loose enemy", user
            // 2026-11-08: a failed Sap, scored against the tank's taunt); and to keep the
            // rogue alive below.
            SpellEntry const* ThreatDrop() const override
            {
                return m_bot.m_spells.rogue.pVanish;
            }

            // Vanish: the enemies on the rogue turn to someone else.
            std::vector<SurvivalSpell> Survival() const override
            {
                SurvivalSpell vanish;
                vanish.spell = m_bot.m_spells.rogue.pVanish;
                vanish.effect = SurvivalEffect::DropThreat;
                vanish.selfOnly = true;
                return { vanish };
            }
        };
    }

    std::unique_ptr<ClassKit> ClassKits::MakeRogue(PartyBotAI const& bot)
    {
        return std::make_unique<RogueKit>(bot);
    }
}
