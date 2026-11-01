#include "Commander/Classes/ClassKit.h"
#include "PartyBotAI.h"

namespace Commander
{
    namespace
    {
        class WarriorKit : public ClassKit
        {
        public:
            using ClassKit::ClassKit;

            std::vector<SpellEntry const*> Interrupts() const override
            {
                auto const& s = m_bot.m_spells.warrior;
                std::vector<SpellEntry const*> out = { s.pPummel };
                if (m_bot.IsWearingShield(m_bot.me))
                    out.push_back(s.pShieldBash);
                return out;
            }

            std::vector<SpellEntry const*> ReflexInterrupts() const override
            {
                auto const& s = m_bot.m_spells.warrior;
                return { s.pPummel, s.pShieldBash };
            }

            // Thunder Clap needs Battle Stance: from Defensive Stance it is cast after a swap
            // (Arbiter -> PartyBotAI::DoCastWithStance), costed by ThreatBehavior.
            std::vector<AreaThreatSpell> AreaThreat() const override
            {
                auto const& s = m_bot.m_spells.warrior;
                return { { s.pThunderClap, 1.0f, false, true, true },
                         { s.pDemoralizingShout, 0.3f, false, false, false },
                         { s.pChallengingShout, 0.0f, true, false, false } };
            }
        };
    }

    std::unique_ptr<ClassKit> ClassKits::MakeWarrior(PartyBotAI const& bot)
    {
        return std::make_unique<WarriorKit>(bot);
    }
}
