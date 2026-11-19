#include "Population/ScenePoints.h"
#include "Database/DatabaseEnv.h"
#include "Log.h"
#include <algorithm>
#include <cctype>

namespace Population
{
    namespace
    {
        // "world": points outside the cities (type hub: a gathering place, Settlements.h).
        char const* const g_cities[] = { "ironforge", "stormwind", "orgrimmar", "undercity", "thunderbluff", "darnassus", "world" };
        char const* const g_types[] = { "ah", "bank", "mail", "idle", "route", "showcase", "hub" };

        template <size_t N>
        bool InList(char const* const (&list)[N], std::string const& value)
        {
            for (char const* item : list)
                if (value == item)
                    return true;
            return false;
        }
    }

    bool ScenePoints::IsValidCity(std::string const& city) { return InList(g_cities, city); }
    bool ScenePoints::IsValidType(std::string const& type) { return InList(g_types, type); }
    char const* ScenePoints::CityList() { return "ironforge stormwind orgrimmar undercity thunderbluff darnassus world"; }
    char const* ScenePoints::TypeList() { return "ah bank mail idle route showcase hub"; }

    bool ScenePoints::IsValidRouteName(std::string const& route)
    {
        if (route.empty() || route.size() > 32)
            return false;
        for (char c : route)
            if (!(std::islower(static_cast<unsigned char>(c)) || std::isdigit(static_cast<unsigned char>(c)) || c == '_'))
                return false;
        return true;
    }

    bool ScenePoints::Load()
    {
        m_points.clear();

        std::unique_ptr<QueryResult> exists(WorldDatabase.Query("SHOW TABLES LIKE 'custom_popbot_point'"));
        m_available = exists != nullptr;
        if (!m_available)
        {
            sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[PopBot] Table custom_popbot_point not found (sql/custom/popbot.sql), no scene points.");
            return false;
        }

        std::unique_ptr<QueryResult> result(WorldDatabase.Query(
            "SELECT `id`, `city`, `type`, `route`, `seq`, `map`, `x`, `y`, `z`, `o`, `capacity` FROM `custom_popbot_point` ORDER BY `city`, `type`, `route`, `seq`, `id`"));
        if (result)
        {
            do
            {
                Field* f = result->Fetch();
                ScenePoint p;
                p.id = f[0].GetUInt32();
                p.city = f[1].GetCppString();
                p.type = f[2].GetCppString();
                p.route = f[3].GetCppString();
                p.seq = f[4].GetUInt32();
                p.mapId = f[5].GetUInt32();
                p.x = f[6].GetFloat();
                p.y = f[7].GetFloat();
                p.z = f[8].GetFloat();
                p.o = f[9].GetFloat();
                p.capacity = std::max<uint32>(1, f[10].GetUInt32());
                m_points.push_back(std::move(p));
            } while (result->NextRow());
        }

        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[PopBot] Loaded %u scene points.", uint32(m_points.size()));
        return true;
    }

    ScenePoint const* ScenePoints::Find(uint32 id) const
    {
        for (ScenePoint const& p : m_points)
            if (p.id == id)
                return &p;
        return nullptr;
    }

    uint32 ScenePoints::NextSeq(std::string const& city, std::string const& route) const
    {
        uint32 next = 1;
        for (ScenePoint const& p : m_points)
            if (p.type == "route" && p.city == city && p.route == route)
                next = std::max(next, p.seq + 1);
        return next;
    }

    uint32 ScenePoints::Add(ScenePoint const& p)
    {
        if (!m_available || !IsValidCity(p.city) || !IsValidType(p.type) || (!p.route.empty() && !IsValidRouteName(p.route)))
            return 0;

        // city / type / route are whitelisted or [a-z0-9_] only, safe to format into the query.
        if (!WorldDatabase.DirectPExecute(
                "INSERT INTO `custom_popbot_point` (`city`, `type`, `route`, `seq`, `map`, `x`, `y`, `z`, `o`, `capacity`) "
                "VALUES ('%s', '%s', '%s', %u, %u, %f, %f, %f, %f, %u)",
                p.city.c_str(), p.type.c_str(), p.route.c_str(), p.seq, p.mapId, p.x, p.y, p.z, p.o, p.capacity))
            return 0;

        uint32 id = 0;
        if (std::unique_ptr<QueryResult> result = WorldDatabase.Query("SELECT MAX(`id`) FROM `custom_popbot_point`"))
            id = result->Fetch()[0].GetUInt32();

        Load();
        return id;
    }

    bool ScenePoints::Remove(uint32 id)
    {
        if (!m_available || !Find(id))
            return false;
        if (!WorldDatabase.DirectPExecute("DELETE FROM `custom_popbot_point` WHERE `id` = %u", id))
            return false;
        Load();
        return true;
    }
}
