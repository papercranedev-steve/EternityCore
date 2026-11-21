#include "Life/Errands/Travel.h"
#include "Life/Requests/Phrases.h"
#include "Commander/Perception/Spatial/TravelTime.h"
#include "Commander/Perception/Spatial/LeaderTrail.h"
#include "Shared/BotChat.h"
#include "Shared/ItemUse.h"
#include "Shared/WalkLength.h"
#include "Player.h"
#include "Spell.h"
#include "SQLStorages.h"
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

        uint32 Clamped(float seconds)
        {
            return std::min<uint32>(MAX_SEC, std::max<uint32>(MIN_SEC, uint32(std::max(0.0f, seconds))));
        }

        // The road's journey at this traveller's pace.
        Commander::TravelTime::Journey Paced(Commander::TravelTime::Journey j, TravelPace const& pace)
        {
            if (pace.skipsSailing && j.sails)
            {
                j.seconds = std::max(0.0f, j.seconds - Commander::TravelTime::SAILING_SEC);
                j.sails = false;
            }
            j.seconds *= pace.timeRate;
            return j;
        }

        // On the road from now for `seconds`, invisible; its position follows the line to `to`
        // when both ends are on the continent it is on.
        void SetOut(Player* me, TravelState& state, WorldLocation const& to, uint32 nowMs, uint32 seconds, bool sails)
        {
            state.phase = TravelState::Phase::OnRoad;
            state.arriveMs = nowMs + seconds * IN_MILLISECONDS;
            state.sailMs = sails ? nowMs + seconds * IN_MILLISECONDS / 2 : 0;
            state.sailSaid = false;
            me->SetVisibility(VISIBILITY_OFF);

            WorldLocation a, b;
            MapEntry const* here = sMapStorage.LookupEntry<MapEntry>(me->GetMapId());
            state.hops = here && here->IsContinent() &&
                Commander::TravelTime::OutsidePoint(Here(me), a) && Commander::TravelTime::OutsidePoint(to, b) &&
                a.mapId == b.mapId && a.mapId == me->GetMapId();
            state.roadFrom = a;
            state.roadTo = b;
            state.roadSinceMs = nowMs;
            state.roadMs = seconds * IN_MILLISECONDS;
            state.nextHopMs = nowMs + HOP_MS;
        }

        // He went on to another map while it waited hidden on his last one (a dungeon, a boat):
        // a new journey from here, quietly (it said where it was when it set out).
        void SetOutAgain(Player* me, TravelState& state, Player* meet, uint32 nowMs)
        {
            state.from = Here(me);
            Commander::TravelTime::Journey const j = Paced(Commander::TravelTime::JourneyOf(state.from, Here(meet), me->GetSpeed(MOVE_RUN)), state.pace);
            SetOut(me, state, Here(meet), nowMs, Clamped(j.seconds), j.sails);
            sLog.Out(LOG_BASIC, LOG_LVL_DETAIL, "[Travel] %s: %s moved on to map %u, on from here.", me->GetName(), meet->GetName(), meet->GetMapId());
        }

        // Its dot on the world map goes along the road (invisible; not into his sight).
        void Hop(Player* me, TravelState& state, Player* meet, uint32 nowMs)
        {
            if (!state.hops || nowMs < state.nextHopMs || me->GetMapId() != state.roadFrom.mapId || me->IsBeingTeleported())
                return;
            state.nextHopMs = nowMs + HOP_MS;
            float const f = std::min(0.95f, float(nowMs - state.roadSinceMs) / float(std::max<uint32>(1, state.roadMs)));
            float const x = state.roadFrom.x + (state.roadTo.x - state.roadFrom.x) * f;
            float const y = state.roadFrom.y + (state.roadTo.y - state.roadFrom.y) * f;
            if (me->GetDistance2d(x, y) < HOP_MIN_YD)
                return;
            if (meet->GetMapId() == me->GetMapId() && meet->GetDistance2d(x, y) < SightYd(meet->GetMap()) + 50.0f)
            {
                state.hops = false;     // close to him now: it stays until the arrival
                return;
            }
            // (the top surface: under a roof or a tree crown does not matter, nobody sees it)
            float const z = me->GetMap()->GetHeight(x, y, 1000.0f, true, 2000.0f);
            if (z <= INVALID_HEIGHT)
                return;
            me->TeleportTo(me->GetMapId(), x, y, z + 0.5f, std::atan2(state.roadTo.y - y, state.roadTo.x - x));
        }

        void Say(Player* me, char const* key, std::vector<std::string> const& tags, Phrases::Vars const& vars)
        {
            std::string const text = Phrases::Say(key, tags, vars);
            if (!text.empty())
                BotShared::BotChat::SayToParty(me, text);
        }

        bool Ok(Player const* me, WorldLocation const& at, float x, float y, float z, float straight)
        {
            BotShared::WalkLength::Point const a{ x, y, z };
            BotShared::WalkLength::Point const b{ at.x, at.y, at.z };
            float const walk = BotShared::WalkLength::Between(me, a, b);
            return walk > 0.0f && walk <= straight * MAX_WALK_FACTOR;
        }

        void FaceAndShow(Player* me, Player* meet)
        {
            me->SetVisibility(VISIBILITY_ON);
            if (meet && me->IsInMap(meet))
                me->SetFacingToObject(meet);
        }

        bool Invisible(TravelState const& state)
        {
            return state.phase == TravelState::Phase::OnRoad || IsHidden(state);
        }
    }

    std::string AreaName(uint32 areaId, Player const* reader)
    {
        AreaEntry const* area = AreaEntry::GetById(areaId);
        if (!area)
            return "";
        std::string name = area->Name ? area->Name : "";
        int const idx = reader && reader->GetSession() ? reader->GetSession()->GetSessionDbLocaleIndex() : -1;
        sObjectMgr.GetAreaLocaleString(area->Id, idx, &name);
        return name;
    }

    bool NearHome(Player const* me)
    {
        WorldLocation const& home = me->GetHomeBind();
        return me->GetMapId() == home.mapId && me->GetDistance2d(home.x, home.y) <= HOME_YD;
    }

    bool HearthReady(Player const* me)
    {
        if (!me->HasItemCount(HEARTHSTONE, 1) || me->IsInCombat())
            return false;
        MapEntry const* home = sMapStorage.LookupEntry<MapEntry>(me->GetHomeBind().mapId);
        if (!home || home->IsDungeon())
            return false;
        SpellEntry const* spell = BotShared::ItemUse::OnUseSpell(me, HEARTHSTONE);
        return spell && me->IsSpellReady(spell, sObjectMgr.GetItemPrototype(HEARTHSTONE));
    }

    bool UseHearth(Player* me)
    {
        if (!me->IsStopped())
            me->StopMoving();
        SpellCastTargets targets;
        targets.setUnitTarget(me);
        return BotShared::ItemUse::UseFromBags(me, HEARTHSTONE, targets);
    }

    bool SpotOutOfSight(Player* me, Player* watcher, float angle, WorldLocation& out)
    {
        Map* map = watcher->GetMap();
        float const sight = SightYd(map);
        WorldLocation const at = Here(watcher);

        // Ground he walked lately: the newest point out of sight, not too far.
        std::vector<Commander::TrailPoint> const trail =
            Commander::LeaderTrail::Copy(watcher->GetObjectGuid(), map->GetId(), map->GetInstanceId());
        for (auto itr = trail.rbegin(); itr != trail.rend(); ++itr)
        {
            if (itr->step != Commander::TrailStep::Ground || itr->magma)
                continue;
            float const d = std::hypot(itr->pos.x - at.x, itr->pos.y - at.y);
            if (d < sight || d > TRAIL_MAX_SIGHTS * sight)
                continue;
            out = WorldLocation(map->GetId(), itr->pos.x, itr->pos.y, itr->pos.z, 0.0f);
            return true;
        }

        // Open ground around him with a walk to him, `angle` first.
        // (further out too: a closed valley, a building, a ledge with no way down near him)
        for (float extra : { 0.0f, 30.0f, 60.0f, 100.0f, 150.0f })
        {
            float const dist = sight + extra;
            for (uint32 i = 0; i < 12; ++i)
            {
                // 0, +30, -30, +60, ... degrees from `angle`
                float const turn = float((i + 1) / 2) * float(M_PI) / 6.0f * ((i % 2) ? 1.0f : -1.0f);
                float const x = at.x + std::cos(angle + turn) * dist;
                float const y = at.y + std::sin(angle + turn) * dist;
                float const z = map->GetHeight(x, y, at.z + 30.0f, true, 80.0f);
                if (z <= INVALID_HEIGHT || std::fabs(z - at.z) > 40.0f || !Ok(me, at, x, y, z, dist))
                    continue;
                out = WorldLocation(map->GetId(), x, y, z, 0.0f);
                return true;
            }
        }
        return false;
    }

    bool InSight(Player const* me, Player const* who)
    {
        return me && who && me->IsInMap(who) && me->IsWithinDist(who, SightYd(who->GetMap()));
    }

    void Begin(Player* me, TravelState& state, Player* meet, std::vector<std::string> const& tags, TravelPace const& pace)
    {
        state = TravelState();
        state.pace = pace;
        state.from = Here(me);
        state.sinceMs = WorldTimer::getMSTime();

        float const run = me->GetSpeed(MOVE_RUN);
        Commander::TravelTime::Journey const j = Paced(Commander::TravelTime::JourneyOf(state.from, Here(meet), run), pace);
        uint32 seconds = Clamped(j.seconds);
        state.fromHereSec = seconds;
        state.fromHereSails = j.sails;

        if (!me->IsStopped())
            me->StopMoving();
        me->GetMotionMaster()->Clear(false, true);
        me->GetMotionMaster()->MoveIdle();

        // Home on the way: the hearthstone first, for real.
        bool hearth = false;
        Commander::TravelTime::Journey home;
        if (HearthReady(me) && !NearHome(me))
        {
            home = Paced(Commander::TravelTime::JourneyOf(me->GetHomeBind(), Here(meet), run), pace);
            uint32 const viaHome = Clamped(home.seconds) + HEARTH_CAST_SEC;
            if (viaHome + HEARTH_GAIN_SEC <= seconds && UseHearth(me))
            {
                hearth = true;
                state.phase = TravelState::Phase::Hearthing;
                state.fromHomeSec = Clamped(home.seconds);
                state.fromHomeSails = home.sails;
                seconds = viaHome;
            }
        }
        if (!hearth)
            SetOut(me, state, Here(meet), state.sinceMs, seconds, j.sails);

        Phrases::Vars vars;
        vars["leader"] = meet->GetName();
        vars["zone"] = AreaName(me->GetZoneId(), meet);
        vars["home"] = AreaName(me->GetHomeBindAreaId(), meet);
        vars["minutes"] = std::to_string(std::max<uint32>(1, (seconds + MINUTE / 2) / MINUTE));
        bool const sails = hearth ? home.sails : j.sails;
        bool const flies = hearth ? home.flies : j.flies;
        Say(me, hearth ? "travel.start.hearth" : sails ? "travel.start.sail" : flies ? "travel.start.fly" : "travel.start", tags, vars);
        sLog.Out(LOG_BASIC, LOG_LVL_DETAIL, "[Travel] %s sets out to %s (map %u -> %u), %u s%s%s%s.", me->GetName(), meet->GetName(),
            me->GetMapId(), meet->GetMapId(), seconds, hearth ? ", hearthstone first" : "", flies ? ", flies" : "", sails ? ", sails" : "");
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
        if (me && state.phase == TravelState::Phase::Hearthing && me->IsNonMeleeSpellCasted(false))
            me->InterruptNonMeleeSpells(false);
        if (me && Invisible(state))
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

            case TravelState::Phase::Hearthing:
            {
                if (InSight(me, meet))
                {
                    Stop(me, state);
                    return Step::None;
                }
                bool const busy = me->IsNonMeleeSpellCasted(false) || me->IsBeingTeleported();
                if (!busy && NearHome(me))
                {
                    state.from = Here(me);
                    SetOut(me, state, Here(meet), now, state.fromHomeSec, state.fromHomeSails);
                }
                else if (!busy)
                {
                    // (the cast broke: on from here)
                    SetOut(me, state, Here(meet), now, state.fromHereSec, state.fromHereSails);
                }
                return Step::OnRoad;
            }

            case TravelState::Phase::OnRoad:
            {
                bool const sight = InSight(me, meet);
                // Summoned, or he walked up to it: it is simply there.
                if (sight && me->IsWithinDist(meet, CLOSE_YD))
                {
                    Stop(me, state);
                    return Step::None;
                }
                if (!sight)
                {
                    if (state.sailMs && !state.sailSaid && now >= state.sailMs)
                    {
                        state.sailSaid = true;
                        Phrases::Vars vars;
                        vars["leader"] = meet->GetName();
                        Say(me, "travel.sail", tags, vars);
                    }
                    if (now < state.arriveMs)
                    {
                        Hop(me, state, meet, now);
                        return Step::OnRoad;
                    }
                }

                Map const* map = meet->GetMap();
                if (!map || map->IsBattleGround())
                    return Step::OnRoad;
                // A dungeon: through its entrance, as a player walks in.
                if (map->IsDungeon() && !me->IsInMap(meet))
                {
                    AreaTriggerTeleport const* in = sObjectMgr.GetMapEntranceTrigger(map->GetId());
                    if (in && me->TeleportTo(in->destination))
                    {
                        me->SetVisibility(VISIBILITY_ON);
                        state = TravelState();
                        return Step::Arrived;
                    }
                    return Step::OnRoad;
                }
                // His map: there it picks the spot (that map's navmesh, its thread).
                state.phase = TravelState::Phase::Hidden;
                state.hiddenSinceMs = now;
                if (!me->IsInMap(meet))
                {
                    if (!me->TeleportTo(meet->GetMapId(), meet->GetPositionX(), meet->GetPositionY(), meet->GetPositionZ(), meet->GetOrientation()))
                        state.phase = TravelState::Phase::OnRoad;
                    return Step::OnRoad;
                }
                // (same map: picks the spot right away)
            }
            // fall through
            case TravelState::Phase::Hidden:
            {
                // He moved on meanwhile: on the road again, from here.
                if (!me->IsInMap(meet))
                {
                    SetOutAgain(me, state, meet, now);
                    return Step::OnRoad;
                }
                if (now < state.nextTryMs)
                    return Step::OnRoad;
                WorldLocation spot;
                float const angle = state.from.mapId == meet->GetMapId()
                    ? std::atan2(state.from.y - meet->GetPositionY(), state.from.x - meet->GetPositionX())
                    : frand(0.0f, 2.0f * float(M_PI));
                if (SpotOutOfSight(me, meet, angle, spot) && me->TeleportTo(spot))
                {
                    state.phase = TravelState::Phase::Revealing;
                    return Step::OnRoad;
                }
                // No spot: where it is, once he cannot see it there (he walked on, or it waits on
                // the road out of his sight); else hidden a while longer (he walks, his trail
                // grows) and only then here, next to him.
                bool const giveUp = WorldTimer::getMSTimeDiff(state.hiddenSinceMs, now) > HIDDEN_GIVE_UP_MS;
                if (InSight(me, meet) && !giveUp)
                {
                    state.nextTryMs = now + HIDDEN_RETRY_MS;
                    return Step::OnRoad;
                }
                sLog.Out(LOG_BASIC, LOG_LVL_DETAIL, "[Travel] %s: no spot out of %s's sight, shows itself here (%s).", me->GetName(),
                    meet->GetName(), giveUp ? "gave up" : "out of his sight");
                FaceAndShow(me, meet);
                state = TravelState();
                return Step::Arrived;
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
