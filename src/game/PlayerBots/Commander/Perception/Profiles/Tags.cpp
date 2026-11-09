#include "Commander/Perception/Profiles/Tags.h"
#include "Commander/Perception/Profiles/ProfileStore.h"
#include "Commander/Perception/Values/ValueModel.h"
#include <algorithm>
#include <vector>

namespace Commander
{
namespace Tags
{
    Engage EngageOf(EnemyFacts const& e)
    {
        if (!e.meleeEnabled)
            return Engage::StandBack;
        if (std::shared_ptr<CreatureProfile const> profile = sProfileStore.GetCreature(e.entry))
            if (profile->mainRangedSpellId)
                return Engage::StandBack;
        return Engage::Melee;
    }

    float EliteMedianHealth(WorldModel const& world)
    {
        std::vector<uint32> health;
        for (EnemyFacts const& e : world.enemies)
            if (e.elite && !e.onAllyOnly && e.maxHealth)
                health.push_back(e.maxHealth);
        if (health.empty())
            return 0.0f;
        std::sort(health.begin(), health.end());
        return float(health[health.size() / 2]);
    }

    Tier TierOf(WorldModel const& world, EnemyFacts const& e, float eliteMedianHealth)
    {
        if (e.elite)
        {
            if (eliteMedianHealth > 0.0f && float(e.maxHealth) >= HEAVY_HEALTH_MULT * eliteMedianHealth)
                return Tier::Heavy;
            return Tier::Elite;
        }
        float const dps = Value::DealtDps(world);
        if (dps > 0.0f && float(e.maxHealth) / dps < FODDER_TTK_SEC)
            return Tier::Fodder;
        return Tier::Normal;
    }

    TeamLedger LedgerOf(WorldModel const& world)
    {
        TeamLedger l;
        for (PartyMemberFacts const& m : world.party)
        {
            if (!m.isBot || m.isPet || !m.CanAct())
                continue;
            if (!m.areaSpells.empty())
            {
                float cheapest = m.areaSpells.front().mana;
                for (AreaSpellFacts const& a : m.areaSpells)
                    cheapest = std::min(cheapest, a.mana);
                if (m.areaChannel.spellId || float(m.mana) >= cheapest)
                    ++l.areaDealers;
            }
            for (ControlAbility const& a : m.controls)
                if (a.Kind() == ControlKind::Mez && !a.peacefulOnly)
                    l.mezTypes |= a.creatureTypeMask ? a.creatureTypeMask : ~0u;
        }
        return l;
    }

    char const* Name(Engage engage)
    {
        switch (engage)
        {
            case Engage::Melee:     return "melee";
            case Engage::StandBack: return "stands back";
        }
        return "?";
    }

    char const* Name(Tier tier)
    {
        switch (tier)
        {
            case Tier::Fodder: return "fodder";
            case Tier::Normal: return "normal";
            case Tier::Elite:  return "elite";
            case Tier::Heavy:  return "heavy";
        }
        return "?";
    }
}
}
