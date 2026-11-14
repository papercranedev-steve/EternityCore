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

            // Feint on the enemy hitting it (10 s cooldown, user 2026-11-14). Vanish also as a
            // way to shake every enemy off (team rescue "loose enemy", user 2026-11-08: a failed
            // Sap, scored against the tank's taunt); and to keep the rogue alive below.
            std::vector<SpellEntry const*> ThreatDrops() const override
            {
                auto const& s = m_bot.m_spells.rogue;
                return { s.pFeint, s.pVanish };
            }

            // Vanish: the enemies on the rogue turn to someone else.
            std::vector<SurvivalSpell> Survival() const override
            {
                SurvivalSpell vanish;
                vanish.spell = m_bot.m_spells.rogue.pVanish;
                vanish.effect = SurvivalEffect::DropThreat;
                vanish.selfOnly = true;
                // Evasion (5 min): +50% dodge, about half the melee hits.
                SurvivalSpell evasion;
                evasion.spell = m_bot.m_spells.rogue.pEvasion;
                evasion.effect = SurvivalEffect::ReducePhysical;
                evasion.fraction = 0.5f;
                evasion.selfOnly = true;
                return { vanish, evasion };
            }

            // Adrenaline Rush (5 min): energy twice as fast. Blade Flurry (2 min): the hits also
            // strike a second enemy. Cold Blood (3 min): the next Eviscerate crits.
            std::vector<BurstSpell> Bursts() const override
            {
                auto const& s = m_bot.m_spells.rogue;
                BurstSpell rush;
                rush.spell = s.pAdrenalineRush;
                rush.damageMult = 0.6f;
                BurstSpell flurry;
                flurry.spell = s.pBladeFlurry;
                flurry.damageMult = 0.5f;
                flurry.minEnemiesNear = 2;
                BurstSpell cold;
                cold.spell = s.pColdBlood;
                cold.then = s.pEviscerate;
                cold.extraSec = 1.5f;
                cold.needsCombo = true;
                return { rush, flurry, cold };
            }
        };
    }

    std::unique_ptr<ClassKit> ClassKits::MakeRogue(PartyBotAI const& bot)
    {
        return std::make_unique<RogueKit>(bot);
    }
}
