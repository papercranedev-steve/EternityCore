#include "Population/Observers.h"
#include "Companions/CompanionMgr.h"
#include "Database/DatabaseEnv.h"
#include "Config/Config.h"
#include "Map.h"
#include "MapManager.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include <algorithm>

namespace Population
{
    void Observers::LoadConfig()
    {
        m_enabled = sConfig.GetBoolDefault("PopBot.Prewarm.Enable", true);
        int32 const waitSec = sConfig.GetIntDefault("PopBot.Prewarm.WaitSec", 90);
        m_waitMs = uint32(std::min(std::max(waitSec, 10), 1800)) * IN_MILLISECONDS;
        if (!m_enabled)
        {
            m_pending.clear();
            m_arrivals.clear();
        }
    }

    void Observers::ExpectAccount(uint32 accountId)
    {
        if (m_enabled && accountId)
            m_pending.push_back({ accountId, 0 });
    }

    void Observers::ExpectCharacter(uint32 accountId, uint32 guid)
    {
        if (m_enabled && accountId && guid)
            m_pending.push_back({ accountId, guid });
    }

    bool Observers::Resolve(Pending const& pending, uint32 nowMs)
    {
        // The last played character, not a companion (they can live on the player's account)
        // and not one that is online already (a bot).
        std::unique_ptr<QueryResult> result;
        if (pending.guid)
            result = CharacterDatabase.PQuery(
                "SELECT `guid`, `race`, `map`, `zone`, `position_x`, `position_y`, `position_z` FROM `characters`"
                " WHERE `guid` = %u AND `account` = %u", pending.guid, pending.accountId);
        else
            result = CharacterDatabase.PQuery(
                "SELECT `guid`, `race`, `map`, `zone`, `position_x`, `position_y`, `position_z` FROM `characters`"
                " WHERE `account` = %u AND `online` = 0 ORDER BY `logout_time` DESC LIMIT 10", pending.accountId);
        if (!result)
            return false;

        do
        {
            Field* f = result->Fetch();
            Observer o;
            o.guid = f[0].GetUInt32();
            if (!pending.guid && sCompanionMgr.IsCompanion(o.guid))
                continue;
            o.accountId = pending.accountId;
            o.team = uint32(Player::TeamForRace(f[1].GetUInt8()));
            o.mapId = f[2].GetUInt32();
            o.zone = f[3].GetUInt32();
            o.x = f[4].GetFloat();
            o.y = f[5].GetFloat();
            o.z = f[6].GetFloat();
            o.arriving = true;
            o.expireMs = nowMs + m_waitMs;

            // One expected character per account: picking another one moves the observer.
            m_arrivals.erase(std::remove_if(m_arrivals.begin(), m_arrivals.end(),
                [&o](Observer const& a) { return a.accountId == o.accountId; }), m_arrivals.end());
            m_arrivals.push_back(o);
            return true;
        } while (result->NextRow());
        return false;
    }

    bool Observers::Update(uint32 nowMs)
    {
        bool added = false;
        std::vector<Pending> pending;
        pending.swap(m_pending);
        for (Pending const& p : pending)
            added |= Resolve(p, nowMs);

        m_observers.clear();
        for (auto const& itr : sObjectAccessor.GetPlayers())
        {
            Player* p = itr.second;
            if (!p || !p->IsInWorld() || p->IsBot())
                continue;
            Observer o;
            o.guid = p->GetGUIDLow();
            o.accountId = p->GetSession()->GetAccountId();
            o.team = uint32(p->GetTeam());
            o.mapId = p->GetMapId();
            o.zone = p->GetCachedZoneId();
            o.x = p->GetPositionX();
            o.y = p->GetPositionY();
            o.z = p->GetPositionZ();
            m_observers.push_back(o);
        }

        // Arrived (same account in the world: also covers picking a character we did not expect) or gave up.
        m_arrivals.erase(std::remove_if(m_arrivals.begin(), m_arrivals.end(), [this, nowMs](Observer const& a)
        {
            if (nowMs >= a.expireMs)
                return true;
            return std::any_of(m_observers.begin(), m_observers.end(),
                [&a](Observer const& o) { return o.accountId == a.accountId; });
        }), m_arrivals.end());

        m_observers.insert(m_observers.end(), m_arrivals.begin(), m_arrivals.end());
        return added;
    }

    bool Observers::CanSee(uint32 mapId, float x, float y, float z) const
    {
        float const CLOSE = 20.0f;      // around a corner: too close to pop in
        float const MARGIN = 15.0f;     // the player moves before the bot does

        Map const* map = sMapMgr.FindMap(mapId, 0);
        float const visible = (map ? map->GetVisibilityDistance() : 100.0f) + MARGIN;

        for (Observer const& o : m_observers)
        {
            if (o.arriving || o.mapId != mapId)
                continue;
            float const dx = o.x - x, dy = o.y - y, dz = o.z - z;
            float const distSq = dx * dx + dy * dy + dz * dz;
            if (distSq > visible * visible)
                continue;
            if (distSq <= CLOSE * CLOSE || !map)
                return true;
            if (map->isInLineOfSight(o.x, o.y, o.z + 2.0f, x, y, z + 2.0f))
                return true;
        }
        return false;
    }

    bool Observers::IsArrivingOwner(uint32 guid) const
    {
        return std::any_of(m_arrivals.begin(), m_arrivals.end(), [guid](Observer const& a) { return a.guid == guid; });
    }
}
