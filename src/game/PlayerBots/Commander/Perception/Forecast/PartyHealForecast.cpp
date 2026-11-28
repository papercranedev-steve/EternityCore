#include "Commander/Perception/Forecast/PartyHealForecast.h"
#include "Commander/Perception/Values/ValueModel.h"
#include "TeamBotAI.h"
#include "HealSpells.h"
#include "Player.h"
#include "Group.h"
#include "Map.h"
#include "SpellEntry.h"
#include <algorithm>

namespace Commander
{
namespace PartyHealForecast
{
    namespace
    {
        int PartyHealEffect(SpellEntry const* spell)
        {
            for (uint32 i = 0; i < MAX_SPELL_EFFECTS; ++i)
                if (spell->Effect[i] == SPELL_EFFECT_HEAL &&
                    (spell->EffectImplicitTargetA[i] == TARGET_ENUM_UNITS_PARTY_WITHIN_CASTER_RANGE ||
                     spell->EffectImplicitTargetB[i] == TARGET_ENUM_UNITS_PARTY_WITHIN_CASTER_RANGE))
                    return int(i);
            return -1;
        }

        // In the caster's party as the core counts it (header).
        bool InCastersParty(Player* me, Unit const* unit)
        {
            Player const* owner = unit->GetCharmerOrOwnerPlayerOrPlayerItself();
            if (!owner)
                return false;
            Group const* group = me->GetGroup();
            if (!group)
                return owner == me;
            return owner->GetGroup() == group && owner->GetSubGroup() == me->GetSubGroup();
        }

        // Healing that lands on `m` before `castMs`: the others' direct heals being cast and
        // the HoT ticks.
        float HealedBefore(WorldModel const& world, PartyMemberFacts const& m, ObjectGuid self, uint32 castMs)
        {
            float healed = 0.0f;
            for (PartyMemberFacts const& p : world.party)
                if (p.guid != self && p.pendingHeal.target == m.guid && p.pendingHeal.landMs <= castMs)
                    healed += p.pendingHeal.amount;
            for (PartyHot const& h : m.hots)
            {
                if (!h.periodMs || h.nextTickMs > castMs)
                    continue;
                uint32 const ticks = std::min(h.TicksLeft(), 1 + (castMs - h.nextTickMs) / h.periodMs);
                healed += h.perTick * float(ticks);
            }
            return healed;
        }
    }

    bool IsPartyHeal(SpellEntry const* spell)
    {
        return spell && PartyHealEffect(spell) >= 0;
    }

    Result Of(TeamBotAI const* bot, WorldModel const& world, SpellEntry const* spell, float castSec)
    {
        Result out;
        Player* me = bot->me;
        int const effect = spell && me ? PartyHealEffect(spell) : -1;
        if (effect < 0)
            return out;
        float const radius = Spells::GetSpellRadius(sSpellRadiusStore.LookupEntry(spell->EffectRadiusIndex[effect]));
        uint32 const castMs = uint32(std::max(0.0f, castSec) * 1000.0f);

        for (PartyMemberFacts const& m : world.party)
        {
            if (!m.alive || m.charmed)
                continue;
            Unit* unit = me->GetMap()->GetUnit(m.guid);
            if (!unit || !unit->IsAlive() || !InCastersParty(me, unit))
                continue;
            if (unit != me && !me->IsWithinDistInMap(unit, radius))
                continue;
            if (unit->GetLevel() + 10 < spell->spellLevel)
                continue;

            Reach r;
            r.guid = m.guid;
            r.heal = HealSpells::Estimate(me, unit, spell).direct;
            float const missing = float(m.maxHealth - m.health) + Value::TakenDps(world, m.guid) * castSec -
                                  HealedBefore(world, m, me->GetObjectGuid(), castMs);
            r.missing = std::max(0.0f, std::min(float(m.maxHealth), missing));
            r.effective = std::min(r.heal, r.missing);
            out.heal += r.heal;
            out.effective += r.effective;
            if (r.missing > 0.0f)
            {
                out.hurtHeal += r.heal;
                ++out.hurt;
            }
            out.targets.push_back(r);
        }
        return out;
    }
}
}
