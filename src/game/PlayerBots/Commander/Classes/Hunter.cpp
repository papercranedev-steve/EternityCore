#include "Commander/Classes/ClassKit.h"
#include "PartyBotAI.h"

namespace Commander
{
    namespace
    {
        class HunterKit : public ClassKit
        {
        public:
            using ClassKit::ClassKit;

            // Feign Death: the enemies on the hunter turn to someone else (it may be resisted).
            std::vector<SurvivalSpell> Survival() const override
            {
                SurvivalSpell fd;
                fd.spell = m_bot.m_spells.hunter.pFeignDeath;
                fd.effect = SurvivalEffect::DropThreat;
                fd.selfOnly = true;
                return { fd };
            }

            // Feign Death also to shake off an enemy that turned to the hunter (30 s cooldown,
            // user 2026-11-14), not only when it is about to die.
            std::vector<SpellEntry const*> ThreatDrops() const override
            {
                return { m_bot.m_spells.hunter.pFeignDeath };
            }

            // Concussive Shot (ranged), Wing Clip (melee).
            std::vector<SpellEntry const*> FleeStoppers() const override
            {
                auto const& s = m_bot.m_spells.hunter;
                return { s.pConcussiveShot, s.pWingClip };
            }
        };
    }

    std::unique_ptr<ClassKit> ClassKits::MakeHunter(PartyBotAI const& bot)
    {
        return std::make_unique<HunterKit>(bot);
    }
}
