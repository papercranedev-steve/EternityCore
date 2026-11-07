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

            // Concussion Blow (talent, 5 s stun) stops a cast too; the tank rotation no longer
            // casts it by itself (Rotations/WarriorTankRotation.cpp).
            std::vector<SpellEntry const*> Interrupts() const override
            {
                auto const& s = m_bot.m_spells.warrior;
                std::vector<SpellEntry const*> out = { s.pPummel };
                if (m_bot.IsWearingShield(m_bot.me))
                    out.push_back(s.pShieldBash);
                out.push_back(s.pConcussionBlow);
                return out;
            }

            SpellEntry const* OpenerCharge() const override
            {
                return m_bot.m_spells.warrior.pCharge;
            }

            // Intimidating Shout: its target cowers, everyone around runs (rage, 3 min).
            std::vector<SpellEntry const*> Controls() const override
            {
                return { m_bot.m_spells.warrior.pIntimidatingShout };
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

            // Shield Wall (-75% damage, Defensive Stance: swapped to, a shield). Last Stand: +30%
            // of maximum health for 20 s (the triggered spell 12976 is scripted, its data says 0).
            // Both were taken out of the tank rotation 2026-11-02 for this decision.
            std::vector<SurvivalSpell> Survival() const override
            {
                auto const& s = m_bot.m_spells.warrior;
                SurvivalSpell wall;
                wall.spell = s.pShieldWall;
                wall.effect = SurvivalEffect::Reduce;
                wall.selfOnly = true;
                wall.needsShield = true;
                SurvivalSpell stand;
                stand.spell = s.pLastStand;
                stand.effect = SurvivalEffect::ExtraHealth;
                stand.fraction = 0.3f;
                stand.durationSec = 20.0f;
                stand.selfOnly = true;
                return { wall, stand };
            }
        };
    }

    std::unique_ptr<ClassKit> ClassKits::MakeWarrior(PartyBotAI const& bot)
    {
        return std::make_unique<WarriorKit>(bot);
    }
}
