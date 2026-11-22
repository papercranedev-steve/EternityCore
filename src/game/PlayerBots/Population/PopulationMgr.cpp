#include "Population/PopulationMgr.h"
#include "Population/PopulationBotAI.h"
#include "Population/HiddenSpot.h"
#include "Companions/CompanionMgr.h"
#include "PlayerBotMgr.h"
#include <cmath>
#include "Player.h"
#include "Chat.h"
#include "ObjectAccessor.h"
#include "MapManager.h"
#include "Config/Config.h"
#include "Log.h"
#include "Utilities/Random.h"
#include <algorithm>

namespace Population
{
    namespace
    {
        // The AI gives up walking out after 2 minutes (MoveTo deadline); this is the backstop.
        uint32 const LEAVE_TIMEOUT_MS = 150 * IN_MILLISECONDS;

        int64 SteadyNowMs()
        {
            return std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
        }

        uint32 Clamp(int32 value, uint32 lo, uint32 hi)
        {
            if (value < int32(lo))
                return lo;
            if (uint32(value) > hi)
                return hi;
            return uint32(value);
        }

        Player* OnlinePlayer(BotHandle const& handle)
        {
            if (!handle.alive || !handle.ai)
                return nullptr;
            // After logout the AI can outlive its Player until the session is deleted;
            // the entry state turns OFFLINE before the Player goes away.
            if (!handle.ai->botEntry || handle.ai->botEntry->state != PB_STATE_ONLINE)
                return nullptr;
            Player* me = handle.ai->me;
            return (me && me->IsInWorld()) ? me : nullptr;
        }

        // More than `behindYd` behind a moving observer (along his heading). Never for one
        // standing still.
        bool IsBehind(Observer const& o, float px, float py, float behindYd)
        {
            float const speed = o.Speed();
            if (speed < Observer::MOVING_SPEED)
                return false;
            float const along = ((px - o.x) * o.vx + (py - o.y) * o.vy) / speed;
            return along < -behindYd;
        }
    }

    char const* BotKindName(BotKind kind)
    {
        switch (kind)
        {
            case BotKind::Test:  return "test";
            case BotKind::City:  return "city";
            case BotKind::Field: return "field";
            case BotKind::Town:  return "town";
            case BotKind::Herald: return "herald";
            case BotKind::Traveller: return "traveller";
        }
        return "?";
    }

    char const* GearTierName(GearTier tier)
    {
        switch (tier)
        {
            case GearTier::Normal:   return "normal";
            case GearTier::PreRaid:  return "preraid";
            case GearTier::Showcase: return "showcase";
        }
        return "?";
    }

    char const* CityRoleName(CityRole role)
    {
        switch (role)
        {
            case CityRole::None:     return "-";
            case CityRole::Stand:    return "stand";
            case CityRole::Mover:    return "mover";
            case CityRole::Showcase: return "showcase";
            case CityRole::Crafter:  return "crafter";
            case CityRole::Sitter:   return "sitter";
            case CityRole::Gatherer: return "gatherer";
        }
        return "?";
    }

    PopulationMgr& PopulationMgr::Instance()
    {
        static PopulationMgr instance;
        return instance;
    }

    PopulationMgr::PopulationMgr()
    {
        m_statResetAt.store(SteadyNowMs());
    }

    void PopulationMgr::LoadConfig()
    {
        bool const wasEnabled = IsEnabled();

        m_enabled.store(sConfig.GetBoolDefault("PopBot.Enable", false));
        m_updateMs.store(Clamp(sConfig.GetIntDefault("PopBot.UpdateMs", 1000), 200, 5000));
        m_maxBots = Clamp(sConfig.GetIntDefault("PopBot.MaxBots", 100), 0, 500);
        m_spawnGapMs = Clamp(sConfig.GetIntDefault("PopBot.SpawnGapMs", 3000), 500, 60000);
        m_prewarmPerTick = Clamp(sConfig.GetIntDefault("PopBot.Prewarm.PerTick", 4), 1, 50);

        uint32 const minMinutes = Clamp(sConfig.GetIntDefault("PopBot.LifetimeMinMinutes", 10), 1, 24 * 60);
        uint32 const maxMinutes = Clamp(sConfig.GetIntDefault("PopBot.LifetimeMaxMinutes", 40), minMinutes, 24 * 60);
        m_lifetimeMinMs = minMinutes * MINUTE * IN_MILLISECONDS;
        m_lifetimeMaxMs = maxMinutes * MINUTE * IN_MILLISECONDS;

        m_city.LoadConfig();
        m_town.LoadConfig();
        m_field.LoadConfig();
        m_travel.LoadConfig();
        m_pacing.LoadConfig();
        m_roster.LoadConfig();
        m_observers.LoadConfig();

        if (wasEnabled && !IsEnabled())
        {
            m_queue.clear();
            DespawnAll();
        }
    }

    void PopulationMgr::LoadData()
    {
        m_identity.Load();
        m_points.Load();
        ReloadAnchors();
        m_field.LoadQuests();
        m_roster.Load();
        // Members already online stay taken (queued requests get their member in SpawnNow).
        for (Record const& r : m_bots)
            if (r.request.rosterId && r.handle->alive)
                m_roster.MarkInUse(r.request.rosterId);
        m_dataLoaded = true;
    }

    void PopulationMgr::ReloadAnchors()
    {
        m_city.LoadAnchors(m_points);
        // Towns keep pointers into the settlements: both are rebuilt together. Town bots keep
        // their settlement id (ids follow the same world data, stable between reloads).
        m_settlements.Load(m_points);
        m_town.LoadAnchors(m_settlements);
        // Roads between the settlements, built in the background (TravellerDirector::Update).
        m_travel.BeginRoads(m_settlements, m_city, m_points);
    }

    void PopulationMgr::EnsureDataLoaded()
    {
        if (!m_dataLoaded)
            LoadData();
    }

    void PopulationMgr::SetEnabled(bool on)
    {
        m_enabled.store(on);
        if (!on)
        {
            m_queue.clear();
            DespawnAll();
        }
    }

    uint32 PopulationMgr::RandomLifetimeMs() const
    {
        return urand(m_lifetimeMinMs, m_lifetimeMaxMs);
    }

    void PopulationMgr::Update(uint32 diff)
    {
        m_nowMs += diff;

        // Forget bots whose AI is gone (logged out and erased by PlayerBotMgr, or failed to load).
        for (Record const& r : m_bots)
            if (!r.handle->alive && r.request.rosterId)
                m_roster.Release(r.request.rosterId, m_nowMs);
        m_bots.erase(std::remove_if(m_bots.begin(), m_bots.end(),
            [](Record const& r) { return !r.handle->alive; }), m_bots.end());

        if (!IsEnabled())
            return;

        EnsureDataLoaded();

        for (Record& r : m_bots)
        {
            // (field bots and travellers leave only out of sight: RetireFieldBots, RetireTravellers)
            if (!r.removing && r.expireMs && m_nowMs >= r.expireMs && r.request.kind != BotKind::Field &&
                r.request.kind != BotKind::Traveller)
                Retire(r);
            // A city or town bot done with its errands (TownLife::WantsToLeave): out of the place;
            // the director sends someone else in.
            else if (!r.removing && r.handle->done && (r.request.kind == BotKind::City || r.request.kind == BotKind::Town))
                Retire(r);
            // Walking out of sight took too long (stuck): log out where he is.
            else if (r.leaveByMs && m_nowMs >= r.leaveByMs)
            {
                r.leaveByMs = 0;
                RequestRemoval(r);
            }
        }

        UpdateRecruits();
        if (m_observers.Update(m_nowMs))
        {
            // Someone is at the character screen or loading in: fill his surroundings now.
            // A continent nobody has entered since the start does not exist yet (the first
            // player creates it): without it the directors find no spot and nothing is placed.
            for (Observer const& o : m_observers.All())
                if (o.arriving && (o.mapId == 0 || o.mapId == 1))
                    sMapMgr.CreateWorldMap(o.mapId);
            m_roster.ForceUpdate();
            m_city.ForceTick();
        }
        m_roster.Update(m_nowMs, m_identity, m_observers);
        m_city.Update(*this, m_observers, m_nowMs);
        m_town.Update(*this, m_observers, m_nowMs);
        m_pacing.Update(*this, m_observers, m_nowMs);
        m_field.Update(*this, m_observers, m_nowMs);
        m_travel.Update(*this, m_observers, m_nowMs);

        // Prewarm requests (at the front) go out several per tick, the others one per PopBot.SpawnGapMs.
        uint32 burst = 0;
        while (!m_queue.empty())
        {
            bool const prewarm = m_queue.front().prewarm;
            if (prewarm ? burst >= m_prewarmPerTick : m_nowMs < m_nextSpawnMs)
                break;

            SpawnRequest const request = m_queue.front();
            m_queue.pop_front();
            if (GetBotCount() < m_maxBots)
                SpawnNow(request);

            if (!prewarm)
            {
                m_nextSpawnMs = m_nowMs + m_spawnGapMs;
                break;
            }
            ++burst;
        }
    }

    void PopulationMgr::OnCharacterScreen(uint32 accountId)
    {
        if (IsEnabled())
            m_observers.ExpectAccount(accountId);
    }

    void PopulationMgr::OnLoginRequest(uint32 accountId, uint32 charGuid)
    {
        if (IsEnabled())
            m_observers.ExpectCharacter(accountId, charGuid);
    }

    void PopulationMgr::UpdateRecruits()
    {
        for (Record& r : m_bots)
        {
            if (r.handle->recruiter.IsEmpty())
                continue;
            ObjectGuid const recruiterGuid = r.handle->recruiter;
            bool const bonded = r.handle->bonded;
            r.handle->recruiter.Clear();    // a failed recruit can be tried again when asked again
            r.handle->bonded = false;
            if (r.removing || !r.request.rosterId)
                continue;

            Player* bot = OnlinePlayer(*r.handle);
            Player* recruiter = sObjectAccessor.FindPlayer(recruiterGuid);
            if (!bot || !recruiter)
                continue;
            // Someone the player parted with for good, or no room by his side (companion slots
            // doc): he says why and declines (a stranger is not kept then).
            if (m_roster.SaidFarewell(r.request.rosterId))
            {
                if (r.handle->alive && r.handle->ai)
                    r.handle->ai->DeclineRecruit(PopulationBotAI::DeclineReason::Farewell);
                continue;
            }
            // Never anyone above the player (recruiting doc §1: the player would use him).
            if (bot->GetLevel() > recruiter->GetLevel())
            {
                if (r.handle->alive && r.handle->ai)
                    r.handle->ai->DeclineRecruit(PopulationBotAI::DeclineReason::TooHigh);
                continue;
            }
            if (!sCompanionMgr.HasFreeSlot(recruiter))
            {
                Companions::CompanionMgr::SlotCount const slots = sCompanionMgr.SlotsOf(recruiter);
                ChatHandler(recruiter->GetSession()).PSendSysMessage("PBERR;%s;noslot;%u;%u;%u",
                    bot->GetName(), slots.used, slots.total, slots.nextFreeLevel);
                if (r.handle->alive && r.handle->ai)
                    r.handle->ai->DeclineRecruit(PopulationBotAI::DeclineReason::NoSlot);
                continue;
            }
            // A stranger is kept from now on: he joins the recruiter's roster first.
            if (Roster::IsStranger(r.request.rosterId))
            {
                uint32 const memberId = m_roster.Adopt(r.request.rosterId, recruiter->GetGUIDLow());
                if (!memberId)
                    continue;
                r.request.rosterId = memberId;
            }
            // A trial partner first (搭伙), unless he is the player's first companion.
            if (!sCompanionMgr.Recruit(bot, recruiter, r.request.rosterId, !bonded))
                continue;

            // A real character now: the roster never spawns it again, the temporary bot leaves
            // and CompanionMgr logs the saved character in as a party bot.
            m_roster.SetCharGuid(r.request.rosterId, bot->GetGUIDLow());
            RequestRemoval(r);
        }
    }

    bool PopulationMgr::SpawnHerald(Player* player, uint8 classId, std::string const& giverName)
    {
        if (!IsEnabled() || !player || !player->IsInWorld() || !Identity::IsValidCombo(player->GetRace(), classId))
            return false;
        EnsureDataLoaded();
        if (!m_roster.IsActive())
            return false;
        // (he gives up on his own much sooner: PopulationBotAI::UpdateHerald)
        uint32 constexpr HERALD_LIFETIME_MS = 20 * MINUTE * IN_MILLISECONDS;
        Waypoint spot;
        if (!Population::PickHiddenSpot(player->GetMap(), std::vector<Waypoint>(), player->GetPositionX(), player->GetPositionY(),
                player->GetPositionZ(), m_observers, spot))
            return false;

        SpawnRequest r;
        r.kind = BotKind::Herald;
        r.team = player->GetTeam();
        r.race = player->GetRace();
        r.classId = classId;
        r.level = uint8(std::min<uint32>(player->GetLevel(), 60));
        r.minLevel = r.level;
        r.maxLevel = r.level;
        r.mapId = player->GetMapId();
        r.x = spot.x;
        r.y = spot.y;
        r.z = spot.z;
        r.o = std::atan2(player->GetPositionY() - spot.y, player->GetPositionX() - spot.x);
        if (r.o < 0.0f)
            r.o += 2.0f * float(M_PI);
        r.prewarm = true;
        r.lifetimeMs = HERALD_LIFETIME_MS;
        r.seeks = player->GetObjectGuid();
        r.giverName = giverName;
        return QueueSpawn(r);
    }

    bool PopulationMgr::QueueSpawn(SpawnRequest const& request)
    {
        if (!IsEnabled())
            return false;
        if (GetBotCount() + m_queue.size() >= m_maxBots)
            return false;
        if (request.prewarm)
            m_queue.push_front(request);
        else
            m_queue.push_back(request);
        return true;
    }

    void PopulationMgr::SpawnNow(SpawnRequest const& request)
    {
        SpawnRequest r = request;

        if (!r.race && r.raceMask)
        {
            std::vector<uint8> races;
            for (uint8 race = 1; race < MAX_RACES; ++race)
                if ((1u << (race - 1)) & r.raceMask)
                    races.push_back(race);
            if (!races.empty())
                r.race = races[urand(0, uint32(races.size()) - 1)];
        }
        if (!r.race)
            r.race = Identity::RandomRace(r.team == HORDE ? HORDE : ALLIANCE);
        if (!r.classId || !Identity::IsValidCombo(r.race, r.classId))
            r.classId = Identity::RandomClass(r.race);
        if (!r.classId)
        {
            sLog.Out(LOG_BASIC, LOG_LVL_ERROR, "[PopBot] No valid class for race %u.", r.race);
            return;
        }

        // A player could see the spot: log in out of sight and walk there. No such place right
        // now: skip, the director asks again with another spot.
        if ((r.kind == BotKind::City || r.kind == BotKind::Town) && m_observers.CanSee(r.mapId, r.x, r.y, r.z))
        {
            Waypoint entrance;
            if (!PickHiddenSpot(r, r.x, r.y, r.z, entrance))
                return;
            r.hasEntrance = true;
            r.entranceX = entrance.x;
            r.entranceY = entrance.y;
            r.entranceZ = entrance.z;
        }

        // A field spot picked a few seconds ago (the queue waits PopBot.SpawnGapMs): the player
        // walked on, he may see it now. Skip, the director picks another one.
        bool const commanded = r.kind == BotKind::Field || r.kind == BotKind::Traveller;
        if (commanded && m_observers.CanSeeSoon(r.mapId, r.x, r.y, r.z, FieldDirector::LOGIN_LEAD_SEC))
            return;

        // A team of one for the Commander (Commander/Team.h unit); two travellers walking
        // together (one partyKey) are one team.
        if (commanded)
        {
            auto const party = r.partyKey ? m_partyUnits.find(r.partyKey) : m_partyUnits.end();
            if (party != m_partyUnits.end())
            {
                r.unitId = party->second;
                m_partyUnits.erase(party);
            }
            else
            {
                r.unitId = ++m_nextUnitId;
                if (r.partyKey)
                    m_partyUnits[r.partyKey] = r.unitId;
            }
        }

        std::string name;
        uint32 guid = 0;    // 0 = new guid
        if (r.kind != BotKind::Test && m_roster.IsActive())
        {
            if (!TakeFromRoster(r, name, guid))
                return;     // nobody free (at these levels): a little emptier here
        }
        else
        {
            if (r.tier == GearTier::Showcase)
                r.level = 60;
            else if (!r.level && r.minLevel)
                r.level = uint8(urand(r.minLevel, std::max(r.minLevel, r.maxLevel)));
            else if (!r.level)
                r.level = Identity::RandomCityLevel();

            std::set<std::string> reserved;
            for (Record const& rec : m_bots)
                reserved.insert(rec.name);
            m_roster.AddNamesTo(reserved);

            name = m_identity.MakeName(r.race, reserved);
            if (name.empty())
            {
                sLog.Out(LOG_BASIC, LOG_LVL_ERROR, "[PopBot] Could not find a free name for race %u.", r.race);
                return;
            }
        }
        r.level = uint8(std::min<uint32>(std::max<uint32>(r.level, 1), 60));

        auto handle = std::make_shared<BotHandle>();
        // PlayerBotMgr takes ownership of the AI (also when AddBot fails).
        PopulationBotAI* ai = new PopulationBotAI(r, name, handle);
        if (!sPlayerBotMgr.AddBot(ai, guid))
        {
            sLog.Out(LOG_BASIC, LOG_LVL_ERROR, "[PopBot] PlayerBotMgr::AddBot failed for '%s'.", name.c_str());
            if (r.rosterId)
                m_roster.Release(r.rosterId, m_nowMs);
            return;
        }
        // The name cache keeps this guid for the name: the member logs in under it again.
        if (r.rosterId && ai->botEntry)
            m_roster.SetSessionGuid(r.rosterId, uint32(ai->botEntry->playerGUID));

        Record rec;
        rec.handle = std::move(handle);
        rec.request = r;
        rec.name = name;
        rec.bornMs = m_nowMs;
        rec.expireMs = m_nowMs + (r.lifetimeMs ? r.lifetimeMs : RandomLifetimeMs());
        m_bots.push_back(std::move(rec));
    }

    bool PopulationMgr::PickHiddenSpot(SpawnRequest const& request, float x, float y, float z, Waypoint& out) const
    {
        if (request.kind == BotKind::Town)
            return m_town.PickHiddenSpot(request.settlementId, x, y, z, m_observers, out);
        if (request.kind == BotKind::Traveller)
        {
            // The town or capital at the road's end it walked to.
            if (m_town.FindState(request.settlementId))
                return m_town.PickHiddenSpot(request.settlementId, x, y, z, m_observers, out);
            Settlement const* s = m_settlements.Find(request.settlementId);
            return s && s->cityZone && m_city.PickHiddenSpot(s->cityZone, x, y, z, m_observers, out);
        }
        return m_city.PickHiddenSpot(request.cityZone, x, y, z, m_observers, out);
    }

    bool PopulationMgr::TakeFromRoster(SpawnRequest& request, std::string& name, uint32& guid)
    {
        RosterQuery query;
        query.team = request.team == HORDE ? HORDE : ALLIANCE;
        query.zone = request.cityZone;
        query.preferHigh = request.role == CityRole::Showcase;
        // (the first companion is of the player's race, of the class he lacks)
        if (request.kind == BotKind::Herald)
        {
            query.race = request.race;
            query.classId = request.classId;
        }
        query.raceMask = request.raceMask;
        if (request.minLevel)
        {
            query.minLevel = request.minLevel;
            query.maxLevel = std::max(request.minLevel, request.maxLevel);
        }

        RosterPick pick;
        if (!m_roster.Pick(query, m_nowMs, pick))
        {
            // Nobody of the roster fits (user 2026-11-19: a level-60 character in Northshire still
            // sees a full start area): a stranger at these levels, for this visit only.
            std::set<std::string> reserved;
            for (Record const& rec : m_bots)
                reserved.insert(rec.name);
            m_roster.AddNamesTo(reserved);
            if (!m_roster.MakeStranger(query, m_identity, reserved, pick))
                return false;
        }

        request.rosterId = pick.memberId;
        request.race = pick.race;
        request.classId = pick.classId;
        request.level = pick.level;
        request.hasLook = true;
        request.look = pick.look;
        request.hasGearPlan = true;
        request.gear = pick.gear;
        request.money = pick.money;
        request.tags = pick.tags;
        // Veterans are the furthest of the roster, not phase 6 sets: gear follows the owner's team.
        if (request.tier == GearTier::Showcase)
            request.tier = GearTier::Normal;
        name = pick.name;
        guid = pick.sessionGuid;
        return true;
    }

    void PopulationMgr::RequestRemoval(Record& record)
    {
        record.removing = true;
        record.leaveByMs = 0;
        if (record.handle->alive && record.handle->ai && record.handle->ai->botEntry)
            record.handle->ai->botEntry->requestRemoval = true;
    }

    void PopulationMgr::Retire(Record& record)
    {
        Player* me = OnlinePlayer(*record.handle);
        if (!me || (record.request.kind != BotKind::City && record.request.kind != BotKind::Town) ||
            !m_observers.CanSee(me->GetMapId(), me->GetPositionX(), me->GetPositionY(), me->GetPositionZ()))
        {
            RequestRemoval(record);
            return;
        }

        // In sight: walk somewhere nobody looks first (no such place: log out here, like a player).
        Waypoint spot;
        if (!PickHiddenSpot(record.request, me->GetPositionX(), me->GetPositionY(), me->GetPositionZ(), spot))
        {
            RequestRemoval(record);
            return;
        }
        record.removing = true;     // a replacement may come in already
        record.leaveByMs = m_nowMs + LEAVE_TIMEOUT_MS;
        record.handle->leaveX = spot.x;
        record.handle->leaveY = spot.y;
        record.handle->leaveZ = spot.z;
        record.handle->leave = true;
    }

    uint32 PopulationMgr::DespawnAll()
    {
        uint32 count = 0;
        for (Record& r : m_bots)
        {
            if (r.removing && !r.leaveByMs)     // walking out counts: he goes right away
                continue;
            RequestRemoval(r);
            ++count;
        }
        return count;
    }

    bool PopulationMgr::Despawn(ObjectGuid guid)
    {
        for (Record& r : m_bots)
        {
            Player* me = OnlinePlayer(*r.handle);
            if (me && me->GetObjectGuid() == guid)
            {
                if (!r.removing || r.leaveByMs)
                    RequestRemoval(r);
                return true;
            }
        }
        return false;
    }

    CityCounts PopulationMgr::CountPlace(PlaceKey place, size_t anchorCount) const
    {
        CityCounts counts;
        counts.anchorUse.assign(anchorCount, 0);

        auto add = [&counts](SpawnRequest const& r)
        {
            ++counts.total;
            switch (r.role)
            {
                case CityRole::Showcase: ++counts.showcase; break;
                case CityRole::Mover:    ++counts.mover; break;
                case CityRole::Crafter:  ++counts.crafter; break;
                case CityRole::Sitter:   ++counts.sitter; break;
                case CityRole::Gatherer: ++counts.gatherer; break;
                default:                 ++counts.stand; break;
            }
            if (r.anchorIndex >= 0 && size_t(r.anchorIndex) < counts.anchorUse.size())
                ++counts.anchorUse[r.anchorIndex];
        };

        for (Record const& r : m_bots)
            if (!r.removing && place.Has(r.request))
                add(r.request);
        for (SpawnRequest const& q : m_queue)
            if (place.Has(q))
                add(q);
        return counts;
    }

    void PopulationMgr::DespawnPlace(PlaceKey place)
    {
        m_queue.erase(std::remove_if(m_queue.begin(), m_queue.end(),
            [place](SpawnRequest const& q) { return place.Has(q); }), m_queue.end());

        for (Record& r : m_bots)
            if ((!r.removing || r.leaveByMs) && place.Has(r.request))
                RequestRemoval(r);
    }

    bool PopulationMgr::DespawnOnePlace(PlaceKey place)
    {
        // Drop a queued request first: nobody saw it yet.
        for (auto it = m_queue.rbegin(); it != m_queue.rend(); ++it)
        {
            if (place.Has(*it))
            {
                m_queue.erase(std::next(it).base());
                return true;
            }
        }

        for (CityRole role : { CityRole::Stand, CityRole::Sitter, CityRole::Gatherer, CityRole::Crafter, CityRole::Mover, CityRole::Showcase })
        {
            Record* oldest = nullptr;
            for (Record& r : m_bots)
                if (!r.removing && place.Has(r.request) && r.request.role == role)
                    if (!oldest || r.bornMs < oldest->bornMs)
                        oldest = &r;
            if (oldest)
            {
                Retire(*oldest);
                return true;
            }
        }
        return false;
    }

    uint32 PopulationMgr::GetBotCount() const
    {
        return uint32(std::count_if(m_bots.begin(), m_bots.end(), [](Record const& r) { return !r.removing; }));
    }

    std::vector<BotInfo> PopulationMgr::ListBots() const
    {
        std::vector<BotInfo> list;
        for (Record const& r : m_bots)
        {
            BotInfo info;
            info.name = r.name;
            info.kind = r.request.kind;
            info.tier = r.request.tier;
            info.role = r.request.role;
            info.cityZone = r.request.cityZone;
            info.race = r.request.race;
            info.classId = r.request.classId;
            info.level = r.request.level;
            info.rosterId = r.request.rosterId;
            info.removing = r.removing;
            info.ageMs = m_nowMs - r.bornMs;
            info.leftMs = r.expireMs > m_nowMs ? r.expireMs - m_nowMs : 0;
            info.mapId = r.request.mapId;
            info.x = r.request.x;
            info.y = r.request.y;
            info.z = r.request.z;
            if (Player* me = OnlinePlayer(*r.handle))
            {
                info.online = true;
                info.guid = me->GetObjectGuid();
                info.level = uint8(me->GetLevel());
                info.mapId = me->GetMapId();
                info.x = me->GetPositionX();
                info.y = me->GetPositionY();
                info.z = me->GetPositionZ();
            }
            list.push_back(std::move(info));
        }
        return list;
    }

    uint32 PopulationMgr::CountField(Observer const& o, float radius, float behindYd) const
    {
        float const radiusSq = radius * radius;
        auto isNear = [&](uint32 m, float px, float py)
        {
            float const dx = px - o.x, dy = py - o.y;
            return m == o.mapId && dx * dx + dy * dy <= radiusSq && !IsBehind(o, px, py, behindYd);
        };
        uint32 count = 0;
        for (Record const& r : m_bots)
        {
            if (r.removing || r.request.kind != BotKind::Field)
                continue;
            if (Player* me = OnlinePlayer(*r.handle))
                count += isNear(me->GetMapId(), me->GetPositionX(), me->GetPositionY()) ? 1 : 0;
            else
                count += isNear(r.request.mapId, r.request.x, r.request.y) ? 1 : 0;
        }
        for (SpawnRequest const& q : m_queue)
            if (q.kind == BotKind::Field && isNear(q.mapId, q.x, q.y))
                ++count;
        return count;
    }

    void PopulationMgr::DespawnField()
    {
        m_queue.erase(std::remove_if(m_queue.begin(), m_queue.end(),
            [](SpawnRequest const& q) { return q.kind == BotKind::Field; }), m_queue.end());
        for (Record& r : m_bots)
            if ((!r.removing || r.leaveByMs) && r.request.kind == BotKind::Field)
                RequestRemoval(r);
    }

    void PopulationMgr::RetireFieldBots(Observers const& observers, float keepYd, float behindYd)
    {
        float const keepSq = keepYd * keepYd;
        for (Record& r : m_bots)
        {
            if (r.removing || r.request.kind != BotKind::Field)
                continue;
            Player* me = OnlinePlayer(*r.handle);
            if (!me)
                continue;
            bool const expired = r.expireMs && m_nowMs >= r.expireMs;
            bool needed = false;
            for (Observer const& o : observers.All())
            {
                float const dx = o.x - me->GetPositionX(), dy = o.y - me->GetPositionY();
                // Passed by long ago: the people ahead of him matter now.
                if (o.mapId == me->GetMapId() && dx * dx + dy * dy <= keepSq &&
                    !IsBehind(o, me->GetPositionX(), me->GetPositionY(), behindYd))
                    needed = true;
            }
            if (needed && !expired && !r.handle->done)
                continue;
            // Logs out only where nobody sees it (else it goes on until it is out of sight).
            if (observers.CanSee(me->GetMapId(), me->GetPositionX(), me->GetPositionY(), me->GetPositionZ()))
                continue;
            RequestRemoval(r);
        }
    }

    uint32 PopulationMgr::CountTravellers(Observer const& o, float radius) const
    {
        float const radiusSq = radius * radius;
        auto isNear = [&](uint32 m, float px, float py)
        {
            float const dx = px - o.x, dy = py - o.y;
            return m == o.mapId && dx * dx + dy * dy <= radiusSq;
        };
        uint32 count = 0;
        for (Record const& r : m_bots)
        {
            if (r.removing || r.request.kind != BotKind::Traveller)
                continue;
            if (Player* me = OnlinePlayer(*r.handle))
                count += isNear(me->GetMapId(), me->GetPositionX(), me->GetPositionY()) ? 1 : 0;
            else
                count += isNear(r.request.mapId, r.request.x, r.request.y) ? 1 : 0;
        }
        for (SpawnRequest const& q : m_queue)
            if (q.kind == BotKind::Traveller && isNear(q.mapId, q.x, q.y))
                ++count;
        return count;
    }

    void PopulationMgr::DespawnTravellers()
    {
        m_queue.erase(std::remove_if(m_queue.begin(), m_queue.end(),
            [](SpawnRequest const& q) { return q.kind == BotKind::Traveller; }), m_queue.end());
        for (Record& r : m_bots)
            if ((!r.removing || r.leaveByMs) && r.request.kind == BotKind::Traveller)
                RequestRemoval(r);
    }

    void PopulationMgr::RetireTravellers(Observers const& observers)
    {
        for (Record& r : m_bots)
        {
            if (r.removing || r.request.kind != BotKind::Traveller)
                continue;
            Player* me = OnlinePlayer(*r.handle);
            if (!me)
                continue;
            float const x = me->GetPositionX(), y = me->GetPositionY(), z = me->GetPositionZ();
            bool const seenNow = observers.CanSee(me->GetMapId(), x, y, z);
            if (seenNow)
            {
                r.seen = true;
                r.seenMs = m_nowMs;
            }
            bool const done = r.handle->done || (r.expireMs && m_nowMs >= r.expireMs);

            if (!seenNow)
            {
                bool gone = done;
                if (!gone && r.seen && m_nowMs - r.seenMs >= TravellerDirector::GONE_UNSEEN_MS)
                {
                    // Passed by: farther than anyone's sight now.
                    float const farYd = me->GetMap()->GetVisibilityDistance() + TravellerDirector::GONE_BEYOND_YD;
                    gone = true;
                    for (Observer const& o : observers.All())
                        if (o.mapId == me->GetMapId() && (o.x - x) * (o.x - x) + (o.y - y) * (o.y - y) <= farYd * farYd)
                            gone = false;
                }
                if (!gone && !r.seen && m_nowMs - r.bornMs >= TravellerDirector::UNMET_MS)
                    gone = true;    // nobody came his way
                if (gone)
                    RequestRemoval(r);
                continue;
            }

            // At the road's end in someone's sight: on into the town, out of sight, then gone
            // (no hidden spot there: he stands about until nobody looks).
            if (done && !r.handle->leave)
            {
                Waypoint spot;
                if (!PickHiddenSpot(r.request, x, y, z, spot))
                    continue;
                r.removing = true;
                r.leaveByMs = m_nowMs + LEAVE_TIMEOUT_MS;
                r.handle->leaveX = spot.x;
                r.handle->leaveY = spot.y;
                r.handle->leaveZ = spot.z;
                r.handle->leave = true;
            }
        }
    }

    bool PopulationMgr::AnyoneInSight(Observer const& o) const
    {
        Map const* map = sMapMgr.FindMap(o.mapId, 0);
        float const visible = map ? map->GetVisibilityDistance() : 100.0f;
        for (Record const& r : m_bots)
        {
            Player* me = OnlinePlayer(*r.handle);
            if (!me || me->GetMapId() != o.mapId)
                continue;
            float const dx = me->GetPositionX() - o.x, dy = me->GetPositionY() - o.y;
            if (dx * dx + dy * dy <= visible * visible)
                return true;
        }
        return false;
    }

    void PopulationMgr::RecordBotTick(uint64 micros)
    {
        m_statTicks.fetch_add(1, std::memory_order_relaxed);
        m_statTotalMicros.fetch_add(micros, std::memory_order_relaxed);
        uint64 prev = m_statMaxMicros.load(std::memory_order_relaxed);
        while (micros > prev && !m_statMaxMicros.compare_exchange_weak(prev, micros, std::memory_order_relaxed))
            ;
    }

    TickStats PopulationMgr::GetStats() const
    {
        TickStats s;
        s.ticks = m_statTicks.load(std::memory_order_relaxed);
        s.totalMicros = m_statTotalMicros.load(std::memory_order_relaxed);
        s.maxMicros = m_statMaxMicros.load(std::memory_order_relaxed);
        s.elapsedMs = uint64(std::max<int64>(0, SteadyNowMs() - m_statResetAt.load(std::memory_order_relaxed)));
        return s;
    }

    void PopulationMgr::ResetStats()
    {
        m_statTicks.store(0);
        m_statTotalMicros.store(0);
        m_statMaxMicros.store(0);
        m_statResetAt.store(SteadyNowMs());
    }

    BotTickCostScope::~BotTickCostScope()
    {
        auto const micros = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - m_start).count();
        sPopulationMgr.RecordBotTick(uint64(micros));
    }
}
