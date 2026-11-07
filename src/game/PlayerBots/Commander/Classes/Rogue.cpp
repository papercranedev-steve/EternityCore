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
