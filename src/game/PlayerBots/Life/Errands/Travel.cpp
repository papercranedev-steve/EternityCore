#include "Life/Errands/Travel.h"
#include "Life/Requests/Phrases.h"
#include "Commander/Perception/Spatial/TravelTime.h"
#include "Commander/Perception/Spatial/LeaderTrail.h"
#include "Shared/BotChat.h"
#include "Shared/WalkLength.h"
#include "Player.h"
#include "Map.h"
#include "MotionMaster.h"
#include "ObjectMgr.h"
#include "WorldSession.h"
#include "Timer.h"
#include "Log.h"
#include "Utilities/Random.h"
#include <algorithm>
#include <cmath>

namespace Life
{
namespace Travel
{
    namespace
    {
        WorldLocation Here(Player const* p)
        {
            return WorldLocation(p->GetMapId(), p->GetPositionX(), p->GetPositionY(), p->GetPositionZ(), p->GetOrientation());
        }

        float SightYd(Map const* map)
        {
            return (map ? map->GetVisibilityDistance() : 100.0f) + SIGHT_MARGIN_YD;
        }

        // The zone `me` is in, in the language of `reader`'s client.
        std::string ZoneName(Player const* me, Player const* reader)
        {
            AreaEntry const* zone = AreaEntry::GetById(me->GetZoneId());
            if (!zone)
                return "";
            std::string name = zone->Name ? zone->Name : "";
            int const idx = reader && reader->GetSession() ? reader->GetSession()->GetSessionDbLocaleIndex() : -1;
            sObjectMgr.GetAreaLocaleString(zone->Id, idx, &name);
            return name;
        }

        void Say(Player* me, char const* key, std::vector<std::string> const& tags, Phrases::Vars const& vars)
        {
            std::string const text = Phrases::Say(key, tags, vars);
            if (!text.empty())
                BotShared::BotChat::SayToParty(me, text);
        }

        bool Ok(Player const* me, Map* map, WorldLocation const& at, float x, float y, float z, float straight)
        {
            BotShared::WalkLength::Point const a{ x, y, z };
            BotShared::WalkLength::Point const b{ at.x, at.y, at.z };
            float const walk = BotShared::WalkLength::Between(me, a, b);
            return walk > 0.0f && walk <= straight * MAX_WALK_FACTOR;
        }

        // Beyond `meet`'s sight on his map (`me` is on it): see the header. False: none.
        bool PickSpot(Player* me, Player* meet, WorldLocation const& from, WorldLocation& out)
        {
            Map* map = meet->GetMap();
            float const sight = SightYd(map);
            WorldLocation const at = Here(meet);

            // Ground he walked lately: the newest point out of sight, not too far.
            std::vector<Commander::TrailPoint> const trail =
                Commander::LeaderTrail::Copy(meet->GetObjectGuid(), map->GetId(), map->GetInstanceId());
            for (auto itr = trail.rbegin(); itr != trail.rend(); ++itr)
            {
                if (itr->step != Commander::TrailStep::Ground || itr->magma)
                    continue;
                float const d = std::hypot(itr->pos.x - at.x, itr->pos.y - at.y);
                if (d < sight || d > 2.0f * sight)
                    continue;
                out = WorldLocation(map->GetId(), itr->pos.x, itr->pos.y, itr->pos.z, 0.0f);
                return true;
            }

            // Open ground around him with a walk to him, the side it comes from first.
            float base = frand(0.0f, 2.0f * float(M_PI));
            if (from.mapId == at.mapId)
                base = std::atan2(from.y - at.y, from.x - at.x);
            for (float extra : { 0.0f, 30.0f, 60.0f })
            {
                float const dist = sight + extra;
                for (uint32 i = 0; i < 12; ++i)
                {
                    // 0, +30, -30, +60, ... degrees from the side it comes from
                    float const turn = float((i + 1) / 2) * float(M_PI) / 6.0f * ((i % 2) ? 1.0f : -1.0f);
                    float const x = at.x + std::cos(base + turn) * dist;
                    float const y = at.y + std::sin(base + turn) * dist;
                    float const z = map->GetHeight(x, y, at.z + 30.0f, true, 80.0f);
                    if (z <= INVALID_HEIGHT || std::fabs(z - at.z) > 40.0f || !Ok(me, map, at, x, y, z, dist))
                        continue;
                    out = WorldLocation(map->GetId(), x, y, z, 0.0f);
                    return true;
                }
            }
            return false;
        }

        void FaceAndShow(Player* me, Player* meet)
        {
            me->SetVisibility(VISIBILITY_ON);
            if (meet && me->IsInMap(meet))
                me->SetFacingToObject(meet);
        }
    }

    bool InSight(Player const* me, Player const* who)
    {
        return me && who && me->IsInMap(who) && me->IsWithinDist(who, SightYd(who->GetMap()));
    }

    void Begin(Player* me, TravelState& state, Player* meet, std::vector<std::string> const& tags)
    {
        state = TravelState();
        state.phase = TravelState::Phase::OnRoad;
        state.from = Here(me);
        state.sinceMs = WorldTimer::getMSTime();

        Commander::TravelTime::Journey const j = Commander::TravelTime::JourneyOf(state.from, Here(meet), me->GetSpeed(MOVE_RUN));
        uint32 const seconds = std::min<uint32>(MAX_SEC, std::max<uint32>(MIN_SEC, uint32(j.seconds)));
        state.arriveMs = state.sinceMs + seconds * IN_MILLISECONDS;
        if (j.sails)
            state.sailMs = state.sinceMs + seconds * IN_MILLISECONDS / 2;

        if (!me->IsStopped())
            me->StopMoving();
        me->GetMotionMaster()->Clear(false, true);
        me->GetMotionMaster()->MoveIdle();

        Phrases::Vars vars;
        vars["leader"] = meet->GetName();
        vars["zone"] = ZoneName(me, meet);
        vars["minutes"] = std::to_string(std::max<uint32>(1, (seconds + MINUTE / 2) / MINUTE));
        Say(me, j.sails ? "travel.start.sail" : j.flies ? "travel.start.fly" : "travel.start", tags, vars);
        sLog.Out(LOG_BASIC, LOG_LVL_DETAIL, "[Travel] %s sets out to %s (map %u -> %u), %u s%s%s.", me->GetName(), meet->GetName(),
            me->GetMapId(), meet->GetMapId(), seconds, j.flies ? ", flies" : "", j.sails ? ", sails" : "");
    }

    void BeginHidden(Player* me, TravelState& state)
    {
        state = TravelState();
        state.phase = TravelState::Phase::Hidden;
        state.from = Here(me);
        state.sinceMs = WorldTimer::getMSTime();
        state.hiddenSinceMs = state.sinceMs;
        me->SetVisibility(VISIBILITY_OFF);
    }

    void Stop(Player* me, TravelState& state)
    {
        if (me && state.phase != TravelState::Phase::None && state.phase != TravelState::Phase::OnRoad)
            me->SetVisibility(VISIBILITY_ON);
        state = TravelState();
    }

    Step Update(Player* me, TravelState& state, Player* meet, std::vector<std::string> const& tags)
    {
        uint32 const now = WorldTimer::getMSTime();
        switch (state.phase)
        {
            case TravelState::Phase::None:
                return Step::None;

            case TravelState::Phase::OnRoad:
            {
                // He came by, or a warlock summoned it.
                if (InSight(me, meet))
                {
                    Stop(me, state);
                    return Step::None;
                }
                if (state.sailMs && !state.sailSaid && now >= state.sailMs)
                {
                    state.sailSaid = true;
                    Phrases::Vars vars;
                    vars["leader"] = meet->GetName();
                    Say(me, "travel.sail", tags, vars);
                }
                if (now < state.arriveMs)
                    return Step::OnRoad;

                Map const* map = meet->GetMap();
                if (!map || map->IsBattleGround())
                    return Step::OnRoad;
                // A dungeon: through its entrance, as a player walks in.
                if (map->IsDungeon())
                {
                    AreaTriggerTeleport const* in = sObjectMgr.GetMapEntranceTrigger(map->GetId());
                    if (in && me->TeleportTo(in->destination))
                    {
                        state = TravelState();
                        return Step::Arrived;
                    }
                    return Step::OnRoad;
                }
                // His map: there it picks the spot (that map's navmesh, its thread).
                if (!me->IsInMap(meet))
                {
                    me->SetVisibility(VISIBILITY_OFF);
                    if (!me->TeleportTo(meet->GetMapId(), meet->GetPositionX(), meet->GetPositionY(), meet->GetPositionZ(), meet->GetOrientation()))
                    {
                        me->SetVisibility(VISIBILITY_ON);
                        return Step::OnRoad;
                    }
                    state.phase = TravelState::Phase::Hidden;
                    state.hiddenSinceMs = now;
                    return Step::OnRoad;
                }
                me->SetVisibility(VISIBILITY_OFF);
                state.phase = TravelState::Phase::Hidden;
                state.hiddenSinceMs = now;
                // (same map: picks the spot right away)
            }
            // fall through
            case TravelState::Phase::Hidden:
            {
                if (!me->IsInMap(meet))
                {
                    if (WorldTimer::getMSTimeDiff(state.hiddenSinceMs, now) > HIDDEN_GIVE_UP_MS)
                    {
                        Stop(me, state);
                        return Step::None;
                    }
                    return Step::OnRoad;
                }
                WorldLocation spot;
                if (!PickSpot(me, meet, state.from, spot))
                {
                    sLog.Out(LOG_BASIC, LOG_LVL_DETAIL, "[Travel] %s: no spot out of %s's sight, shows itself here.", me->GetName(), meet->GetName());
                    FaceAndShow(me, meet);
                    state = TravelState();
                    return Step::Arrived;
                }
                if (!me->TeleportTo(spot))
                {
                    FaceAndShow(me, meet);
                    state = TravelState();
                    return Step::Arrived;
                }
                state.phase = TravelState::Phase::Revealing;
                return Step::OnRoad;
            }

            case TravelState::Phase::Revealing:
            {
                // (the caller skips its update while the move is under way)
                FaceAndShow(me, meet);
                state = TravelState();
                Phrases::Vars vars;
                vars["leader"] = meet->GetName();
                Say(me, "travel.near", tags, vars);
                return Step::Arrived;
            }
        }
        return Step::None;
    }
}
}
