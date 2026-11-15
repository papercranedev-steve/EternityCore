#include "Commander/Classes/ClassKit.h"
#include "Rotations/ClassRotation.h"
#include "PartyBotAI.h"
#include "DBCStores.h"
#include "SpellMgr.h"

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
                // Deterrence (talent, 5 min): +25% dodge and parry, about half the melee hits.
                SurvivalSpell deterrence;
                deterrence.spell = m_bot.m_spells.hunter.pDeterrence;
                deterrence.effect = SurvivalEffect::ReducePhysical;
                deterrence.fraction = 0.5f;
                deterrence.selfOnly = true;
                return { fd, deterrence };
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

            // Rapid Fire (5 min): +40% ranged attack speed. Bestial Wrath (talent, 2 min): on
            // the pet, +50% pet damage.
            std::vector<BurstSpell> Bursts() const override
            {
                auto const& s = m_bot.m_spells.hunter;
                BurstSpell rapid;
                rapid.spell = s.pRapidFire;
                rapid.damageMult = 0.3f;
                BurstSpell wrath;
                wrath.spell = s.pBestialWrath;
                wrath.damageMult = 0.2f;
                wrath.needsPet = true;
                return { rapid, wrath };
            }

            // Auto Shot's minimum range (8 yd in Spell.dbc 5875), once the hunter has it.
            float MinAttackRange() const override
            {
                if (!m_bot.me->HasSpell(ClassRotations::SPELL_AUTO_SHOT))
                    return 0.0f;
                SpellEntry const* autoShot = sSpellMgr.GetSpellEntry(ClassRotations::SPELL_AUTO_SHOT);
                SpellRangeEntry const* range = autoShot ? sSpellRangeStore.LookupEntry(autoShot->rangeIndex) : nullptr;
                return range ? range->minRange : 0.0f;
            }

            // Wing Clip: melee, 50% slower for 10 s (user 2026-11-15: then step back, else the
            // enemy stays in the hunter's face).
            std::vector<SpellEntry const*> KiteSlows() const override
            {
                return { m_bot.m_spells.hunter.pWingClip };
            }

            // Intimidation (talent, 1 min): the pet's next hit stuns its target for 3 s.
            SpellEntry const* PetStun() const override
            {
                return m_bot.m_spells.hunter.pIntimidation;
            }
        };
    }

    std::unique_ptr<ClassKit> ClassKits::MakeHunter(PartyBotAI const& bot)
    {
        return std::make_unique<HunterKit>(bot);
    }
}
