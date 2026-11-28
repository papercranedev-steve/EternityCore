#include "Commander/Coordination/Duties/TotemAssigner.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/WorldModel.h"
#include "SharedDefines.h"
#include <algorithm>

namespace Commander
{
    namespace
    {
        uint32 SpellOf(std::vector<TotemAbility> const& spells, TotemKind kind)
        {
            for (TotemAbility const& s : spells)
                if (s.kind == uint8(kind))
                    return s.spellId;
            return 0;
        }
    }

    std::vector<TotemKind> TotemAssigner::Order(TotemSlot slot, PartyMix const& mix)
    {
        bool const melee = mix.melee > 0;
        switch (slot)
        {
            case TOTEM_SLOT_WATER:
                if (mix.manaUsers > 0)
                    return { TotemKind::ManaSpring, TotemKind::HealingStream };
                return { TotemKind::HealingStream, TotemKind::ManaSpring };
            case TOTEM_SLOT_EARTH:
                if (mix.controlThreat && melee)
                    return { TotemKind::Tremor, TotemKind::StrengthOfEarth, TotemKind::Stoneskin };
                if (mix.controlThreat)
                    return { TotemKind::Tremor, TotemKind::Stoneskin, TotemKind::StrengthOfEarth };
                if (melee)
                    return { TotemKind::StrengthOfEarth, TotemKind::Stoneskin };
                return { TotemKind::Stoneskin, TotemKind::StrengthOfEarth };
            case TOTEM_SLOT_AIR:
                if (melee)
                    return { TotemKind::Windfury, TotemKind::GraceOfAir };
                return { TotemKind::TranquilAir };
            case TOTEM_SLOT_FIRE:
                if (melee)
                    return { TotemKind::Flametongue };
                return {};
            default:
                return {};
        }
    }

    void TotemAssigner::Assign(WorldModel const& world, TeamPlan& plan) const
    {
        TotemPlan out;
        out.active = true;

        PartyMix mix;
        std::vector<PartyMemberFacts const*> shamans;
        for (PartyMemberFacts const& m : world.party)
        {
            if (m.isPet || !m.alive)
                continue;
            if (m.role == ROLE_TANK || m.role == ROLE_MELEE_DPS)
                ++mix.melee;
            if (m.UsesMana())
                ++mix.manaUsers;
            if (m.isBot && !m.totemSpells.empty())
                shamans.push_back(&m);
            if (m.controlMechanics & TREMOR_MECHANICS)
                mix.controlThreat = true;
        }
        for (EnemyFacts const& e : world.enemies)
            if (e.controlMechanics & TREMOR_MECHANICS)
                mix.controlThreat = true;
        std::stable_sort(shamans.begin(), shamans.end(), [](PartyMemberFacts const* a, PartyMemberFacts const* b)
            { return a->guid.GetRawValue() < b->guid.GetRawValue(); });

        std::vector<TotemKind> taken;
        for (PartyMemberFacts const* s : shamans)
        {
            TotemDuty duty;
            duty.bot = s->guid;
            for (uint8 slot = 0; slot < MAX_TOTEM_SLOT; ++slot)
            {
                for (TotemKind kind : Order(TotemSlot(slot), mix))
                {
                    if (std::find(taken.begin(), taken.end(), kind) != taken.end())
                        continue;
                    uint32 const spellId = SpellOf(s->totemSpells, kind);
                    if (!spellId)
                        continue;
                    duty.spellIds[slot] = spellId;
                    taken.push_back(kind);
                    break;
                }
            }
            out.duties.push_back(duty);
        }
        plan.totems = std::move(out);
    }

    void TotemAssigner::Clear(TeamPlan& plan) const
    {
        plan.totems = TotemPlan();
    }
}
