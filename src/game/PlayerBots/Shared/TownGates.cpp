#include "Shared/TownGates.h"
#include "Database/DatabaseEnv.h"
#include "Log.h"
#include <mutex>

namespace BotShared
{
namespace TownGates
{
    namespace
    {
        std::mutex s_lock;
        std::vector<Gate> s_gates;

        float DistSq(Gate const& g, float x, float y)
        {
            float const dx = g.x - x;
            float const dy = g.y - y;
            return dx * dx + dy * dy;
        }
    }

    void Load()
    {
        CharacterDatabase.DirectExecute(
            "CREATE TABLE IF NOT EXISTS `custom_town_gate` ("
            " `id` INT UNSIGNED NOT NULL AUTO_INCREMENT,"
            " `map` INT UNSIGNED NOT NULL,"
            " `zone` INT UNSIGNED NOT NULL,"
            " `position_x` FLOAT NOT NULL,"
            " `position_y` FLOAT NOT NULL,"
            " `position_z` FLOAT NOT NULL,"
            " `name` VARCHAR(64) NOT NULL DEFAULT '',"
            " PRIMARY KEY (`id`)"
            ") ENGINE=InnoDB DEFAULT CHARSET=utf8 COMMENT='companions: where they wait at a town gate (.partybot gate add)'");

        std::vector<Gate> gates;
        std::unique_ptr<QueryResult> result(CharacterDatabase.Query(
            "SELECT `id`, `map`, `zone`, `position_x`, `position_y`, `position_z`, `name` FROM `custom_town_gate`"));
        if (result)
        {
            do
            {
                Field* f = result->Fetch();
                Gate g;
                g.id = f[0].GetUInt32();
                g.mapId = f[1].GetUInt32();
                g.zoneId = f[2].GetUInt32();
                g.x = f[3].GetFloat();
                g.y = f[4].GetFloat();
                g.z = f[5].GetFloat();
                g.name = f[6].GetCppString();
                gates.push_back(g);
            } while (result->NextRow());
        }

        std::lock_guard<std::mutex> guard(s_lock);
        s_gates = std::move(gates);
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, ">> [TownGates] %u town gates loaded.", uint32(s_gates.size()));
    }

    bool Nearest(uint32 mapId, uint32 zoneId, float x, float y, Gate& out)
    {
        std::lock_guard<std::mutex> guard(s_lock);
        float best = MAX_RANGE_YD * MAX_RANGE_YD;
        bool found = false;
        for (Gate const& g : s_gates)
        {
            if (g.mapId != mapId || g.zoneId != zoneId)
                continue;
            float const d = DistSq(g, x, y);
            if (d <= best)
            {
                best = d;
                out = g;
                found = true;
            }
        }
        return found;
    }

    uint32 Add(uint32 mapId, uint32 zoneId, float x, float y, float z, std::string const& name)
    {
        std::string escaped = name;
        CharacterDatabase.escape_string(escaped);
        if (!CharacterDatabase.DirectPExecute(
                "INSERT INTO `custom_town_gate` (`map`, `zone`, `position_x`, `position_y`, `position_z`, `name`)"
                " VALUES (%u, %u, %f, %f, %f, '%s')", mapId, zoneId, x, y, z, escaped.c_str()))
            return 0;
        std::unique_ptr<QueryResult> id(CharacterDatabase.Query("SELECT MAX(`id`) FROM `custom_town_gate`"));
        if (!id)
            return 0;

        Gate g;
        g.id = id->Fetch()[0].GetUInt32();
        g.mapId = mapId;
        g.zoneId = zoneId;
        g.x = x;
        g.y = y;
        g.z = z;
        g.name = name;
        std::lock_guard<std::mutex> guard(s_lock);
        s_gates.push_back(g);
        return g.id;
    }

    bool RemoveNear(uint32 mapId, float x, float y, float range, Gate& removed)
    {
        {
            std::lock_guard<std::mutex> guard(s_lock);
            float best = range * range;
            auto pick = s_gates.end();
            for (auto itr = s_gates.begin(); itr != s_gates.end(); ++itr)
            {
                if (itr->mapId != mapId)
                    continue;
                float const d = DistSq(*itr, x, y);
                if (d <= best)
                {
                    best = d;
                    pick = itr;
                }
            }
            if (pick == s_gates.end())
                return false;
            removed = *pick;
            s_gates.erase(pick);
        }
        CharacterDatabase.PExecute("DELETE FROM `custom_town_gate` WHERE `id` = %u", removed.id);
        return true;
    }

    std::vector<Gate> OfZone(uint32 mapId, uint32 zoneId)
    {
        std::lock_guard<std::mutex> guard(s_lock);
        std::vector<Gate> out;
        for (Gate const& g : s_gates)
            if (g.mapId == mapId && g.zoneId == zoneId)
                out.push_back(g);
        return out;
    }
}
}
