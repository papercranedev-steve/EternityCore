#include "Commander/Perception/Forecast/ChainHealForecast.h"
#include "Commander/Perception/Spatial/PackResolver.h"
#include "Commander/Perception/Values/ValueModel.h"
#include "PartyBotAI.h"
#include "HealSpells.h"
#include "Player.h"
#include "Map.h"
#include "SpellEntry.h"
#include <algorithm>

namespace Commander
{
namespace ChainHealForecast
{
    Jumps Of(PartyBotAI const* bot, WorldModel const& world, PartyMemberFacts const& first,
             SpellEntry const* spell, float castSec)
    {
        Jumps out;
        Player* me = bot->me;
        if (!spell || !me)
            return out;

        // The chained heal effect, its targets and its falloff.
        int effect = -1;
        for (uint32 i = 0; i < MAX_SPELL_EFFECTS; ++i)
            if (spell->Effect[i] == SPELL_EFFECT_HEAL && spell->EffectImplicitTargetA[i] == TARGET_UNIT_FRIEND_CHAIN_HEAL)
                effect = int(i);
        if (effect < 0)
            return out;
        uint32 targets = spell->EffectChainTarget[effect];
        float multiplier = spell->DmgMultiplier[effect];
        if (Player* modOwner = me->GetSpellModOwner())
        {
            modOwner->ApplySpellMod(spell->Id, SPELLMOD_JUMP_TARGETS, targets, nullptr, false);
            modOwner->ApplySpellMod(spell->Id, SPELLMOD_EFFECT_PAST_FIRST, multiplier, nullptr, false);
        }
        if (targets <= 1 || multiplier <= 0.0f)
            return out;

        // Who it may jump to: hurt members, players by most missing first, then pets.
        std::vector<PartyMemberFacts const*> pool;
        for (PartyMemberFacts const& m : world.party)
            if (m.alive && !m.charmed && m.guid != first.guid && m.health < m.maxHealth)
                pool.push_back(&m);
        std::stable_sort(pool.begin(), pool.end(), [](PartyMemberFacts const* a, PartyMemberFacts const* b)
        {
            if (a->isPet != b->isPet)
                return !a->isPet;
            return a->maxHealth - a->health > b->maxHealth - b->health;
        });

        Position3 prev = first.pos;
        float factor = 1.0f;
        for (uint32 jump = 1; jump < targets; ++jump)
        {
            auto next = std::find_if(pool.begin(), pool.end(), [&prev](PartyMemberFacts const* m)
                { return PackResolver::Distance(prev, m->pos) <= CHAIN_SPELL_JUMP_RADIUS; });
            if (next == pool.end())
                break;
            PartyMemberFacts const& m = **next;
            pool.erase(next);
            factor *= multiplier;
            prev = m.pos;

            Unit const* unit = me->GetMap()->GetUnit(m.guid);
            if (!unit)
                continue;
            float const heal = HealSpells::Estimate(me, unit, spell).direct * factor;
            float const missing = float(m.maxHealth - m.health) + Value::TakenDps(world, m.guid) * castSec;
            out.heal += heal;
            out.effective += std::max(0.0f, std::min(heal, std::min(float(m.maxHealth), missing)));
            ++out.count;
        }
        return out;
    }
}
}
