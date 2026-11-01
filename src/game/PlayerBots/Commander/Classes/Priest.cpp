#include "Commander/Classes/ClassKit.h"
#include "PartyBotAI.h"

namespace Commander
{
    namespace
    {
        class PriestKit : public ClassKit
        {
        public:
            using ClassKit::ClassKit;

            // Silence is a shadow talent: null for most healers.
            std::vector<SpellEntry const*> Interrupts() const override
            {
                return { m_bot.m_spells.priest.pSilence };
            }

            std::vector<SpellEntry const*> ReflexInterrupts() const override
            {
                return { m_bot.m_spells.priest.pSilence };
            }

            bool CanDispel() const override { return true; }

            // Undead only (TargetCreatureType, checked by EscapeBehavior); breaks on damage.
            // Psychic Scream is not a hold spell: feared enemies run into the next pack.
            std::vector<SpellEntry const*> HoldSpells() const override
            {
                return { m_bot.m_spells.priest.pShackleUndead };
            }

            // Weakened Soul is not checked by CanTryToCastSpell: the cast may fail, the caller
            // falls back to its other action.
            SpellEntry const* AllyShield() const override
            {
                return m_bot.m_spells.priest.pPowerWordShield;
            }
        };
    }

    std::unique_ptr<ClassKit> ClassKits::MakePriest(PartyBotAI const& bot)
    {
        return std::make_unique<PriestKit>(bot);
    }
}
