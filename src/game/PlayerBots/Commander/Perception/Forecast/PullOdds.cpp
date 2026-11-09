#include "Commander/Perception/Forecast/PullOdds.h"
#include "Commander/Perception/Spatial/PackResolver.h"
#include "Creature.h"
#include "Item.h"
#include "ItemPrototype.h"
#include "Map.h"
#include "Player.h"
#include "SharedDefines.h"
#include <algorithm>
#include <cstdio>

namespace Commander
{
namespace PullOdds
{
    namespace
    {
        bool Contains(std::vector<ObjectGuid> const& list, ObjectGuid guid)
        {
            return std::find(list.begin(), list.end(), guid) != list.end();
        }

        // Average item level of what `player` wears (shirt and tabard left out). 0 = nothing worn.
        float AverageItemLevel(Player const* player)
        {
            uint32 sum = 0;
            uint32 count = 0;
            for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
            {
                if (slot == EQUIPMENT_SLOT_BODY || slot == EQUIPMENT_SLOT_TABARD)
                    continue;
                Item const* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
                if (!item || !item->GetProto())
                    continue;
                sum += item->GetProto()->ItemLevel;
                ++count;
            }
            return count ? float(sum) / float(count) : 0.0f;
        }
    }

    Edge PartyEdge(WorldModel const& world, Map* map, std::vector<ObjectGuid> const& pulled)
    {
        Edge edge;
        float mobLevel = 0.0f;
        uint32 mobs = 0;
        for (ObjectGuid g : pulled)
        {
            if (Creature const* c = map->GetCreature(g))
            {
                mobLevel += float(c->GetLevel());
                ++mobs;
            }
        }
        if (!mobs)
            return edge;
        mobLevel /= float(mobs);

        float weightSum = 0.0f;
        float levelSum = 0.0f;
        float gearSum = 0.0f;
        float ilvlSum = 0.0f;
        for (PartyMemberFacts const& m : world.party)
        {
            if (m.isPet || !m.alive)
                continue;
            Player const* player = map->GetPlayer(m.guid);
            if (!player)
                continue;
            float const weight = (m.role == ROLE_TANK || m.role == ROLE_HEALER) ? TANK_HEALER_WEIGHT : 1.0f;
            float const ilvl = AverageItemLevel(player);
            float const norm = float(m.level) + GEAR_NORM_OVER_LEVEL;
            weightSum += weight;
            levelSum += weight * (float(m.level) - mobLevel);
            gearSum += weight * (ilvl - norm) * GEAR_LEVELS_PER_ILVL;
            ilvlSum += weight * ilvl;
        }
        if (weightSum <= 0.0f)
            return edge;
        edge.levels = levelSum / weightSum;
        edge.gear = gearSum / weightSum;
        edge.avgItemLevel = ilvlSum / weightSum;
        return edge;
    }

    AddRisk NearbyAdds(WorldModel const& world, Position3 const& at, std::vector<ObjectGuid> const& pulled)
    {
        AddRisk risk;
        for (PackFacts const& p : world.packs)
        {
            for (PackCreature const& c : p.members)
            {
                if (!c.aggressive || c.noAggro || c.tappedByOther || Contains(pulled, c.guid))
                    continue;
                float const d = PackResolver::Distance(c.pos, at);
                if (d >= RISK_FAR_YD)
                    continue;
                float const fade = d <= RISK_NEAR_YD ? 1.0f : (RISK_FAR_YD - d) / (RISK_FAR_YD - RISK_NEAR_YD);
                RiskSource s;
                s.guid = c.guid;
                s.distance = d;
                s.elite = c.elite;
                s.weight = (c.elite ? ELITE_RISK : 1.0f) * fade;
                risk.total += s.weight;
                risk.sources.push_back(s);
            }
        }
        std::sort(risk.sources.begin(), risk.sources.end(),
            [](RiskSource const& a, RiskSource const& b) { return a.weight > b.weight; });
        return risk;
    }

    bool FightThere(Edge const& edge, AddRisk const& risk)
    {
        return edge.Total() - ADD_RISK_LEVELS * risk.total >= FIGHT_THERE_MIN_EDGE;
    }

    std::string Describe(Map* map, Edge const& edge, AddRisk const& risk)
    {
        char buf[160];
        snprintf(buf, sizeof(buf), "edge %+.1f levels (level %+.1f, gear %+.1f, item level %.0f), add risk %.1f",
            edge.Total(), edge.levels, edge.gear, edge.avgItemLevel, risk.total);
        std::string text = buf;
        if (!risk.sources.empty())
        {
            text += " (";
            size_t const shown = std::min<size_t>(risk.sources.size(), 3);
            for (size_t i = 0; i < shown; ++i)
            {
                RiskSource const& s = risk.sources[i];
                Creature const* c = map->GetCreature(s.guid);
                snprintf(buf, sizeof(buf), "%s%s %.0f yd%s", i ? ", " : "", c ? c->GetName() : "?", s.distance,
                    s.elite ? " elite" : "");
                text += buf;
            }
            if (risk.sources.size() > shown)
            {
                snprintf(buf, sizeof(buf), ", %u more", uint32(risk.sources.size() - shown));
                text += buf;
            }
            text += ")";
        }
        return text;
    }
}
}
