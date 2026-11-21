/*
 * Bot daily life (PlayerBots/Life): a bot far from someone it should join travels there on its
 * own, as a player would (design doc .kiro/docs/2026-11-21-companion-presence.md §2.4, user
 * 2026-11-21: "both ends real, the middle simulated"). Used by a party bot left behind that no
 * warlock will summon (PartyBotAI, companions and temporary bots alike: none is teleported to
 * the leader; who summons is the Commander's, GatherPlanner).
 *
 *  - Begin: how long the journey takes (Commander/Perception/Spatial/TravelTime::JourneyOf: run,
 *    flight path, boat), clamped to MIN_SEC..MAX_SEC; one party line where it is and how long
 *    ("我在暮色森林，坐鸟过去，大概 4 分钟。"). Home on the way (its hearthstone ready, the
 *    journey from its bind point HEARTH_GAIN_SEC shorter): it really casts the hearthstone
 *    first and goes on from home ("先用炉石回暴风城"); a broken cast: from here.
 *  - On the road: the bot is invisible (nobody meets it, no creature sees it) and moves along
 *    the straight line between the two places every HOP_MS (its dot on the world map goes his
 *    way; on one continent only, and never into his sight); says "上船了" halfway when it
 *    crosses the sea. Summoned or he walked up to it (within CLOSE_YD): it shows itself and the
 *    travel ends; he only came into sight distance: the arrival below starts at once.
 *  - Arrival: it appears out of his sight and walks the last stretch (the caller's walk,
 *    Commander/Behaviors/Movement/Arrival). Where:
 *      his map is a dungeon: its entrance inside (as a player walks in through the portal);
 *      else on his map, beyond the visibility distance: first ground he walked lately
 *      (LeaderTrail), else open ground around him with a walk to him (from the side the bot
 *      comes from). On another map it first goes there hidden (invisible, next to him), picks
 *      the spot on that map's thread, moves there and shows itself. He went on to another map
 *      meanwhile: a new journey from there (quietly), never a teleport to him.
 *    Nowhere hidden found: it stays hidden and looks again every HIDDEN_RETRY_MS; it shows
 *    itself where it is once he cannot see it there, or after HIDDEN_GIVE_UP_MS (logged).
 *  - Not to a battleground: it waits on the road.
 *
 * Map thread of the bot (Update runs on the map it is on: the spot is picked after the hidden
 * teleport, on his map).
 */

#ifndef MANGOS_LIFE_TRAVEL_H
#define MANGOS_LIFE_TRAVEL_H

#include "Common.h"
#include "SharedDefines.h"
#include <string>
#include <vector>

class Player;

namespace Life
{
    // How fast this traveller gets about (a companion's friend trait, Companions/Affinity/Traits:
    // 熟路, 神行). Default: as the road says.
    struct TravelPace
    {
        float timeRate = 1.0f;
        bool skipsSailing = false;      // no wait for the ship at the docks
    };

    // Per bot (a member of its AI). Plain data.
    struct TravelState
    {
        enum class Phase : uint8
        {
            None,
            Hearthing,      // casting its hearthstone (it goes on from home)
            OnRoad,
            Hidden,         // teleported to his map invisible; the spot is picked there
            Revealing,      // moved to the spot; visible again once the move is done
        };
        Phase phase = Phase::None;
        WorldLocation from;         // where it set out
        uint32 sinceMs = 0;
        uint32 arriveMs = 0;
        uint32 sailMs = 0;          // 0: no sea to cross
        bool sailSaid = false;
        uint32 hiddenSinceMs = 0;
        uint32 nextTryMs = 0;       // hidden: the next look for a spot out of his sight
        // Hearthing: the journey from home, and the one from here if the cast breaks.
        uint32 fromHomeSec = 0;
        bool fromHomeSails = false;
        uint32 fromHereSec = 0;
        bool fromHereSails = false;
        // On the road: the line its position follows (outside points of both ends, one
        // continent), since when and how long.
        bool hops = false;
        WorldLocation roadFrom, roadTo;
        uint32 roadSinceMs = 0;
        uint32 roadMs = 0;
        uint32 nextHopMs = 0;
        TravelPace pace;            // this journey's (Begin)
    };

    namespace Travel
    {
        constexpr uint32 MIN_SEC = 20;
        constexpr uint32 MAX_SEC = 10 * MINUTE;
        constexpr float SIGHT_MARGIN_YD = 25.0f;    // beyond his visibility distance
        constexpr float MAX_WALK_FACTOR = 2.5f;     // the walk in at most this times the straight way
        // Hidden next to him with no spot out of his sight: tries again every HIDDEN_RETRY_MS
        // (he walks, his trail grows); shows itself there after HIDDEN_GIVE_UP_MS.
        constexpr uint32 HIDDEN_RETRY_MS = 3 * IN_MILLISECONDS;
        constexpr uint32 HIDDEN_GIVE_UP_MS = 60 * IN_MILLISECONDS;
        // Ground he walked lately counts up to this many sight distances away.
        constexpr float TRAIL_MAX_SIGHTS = 3.0f;
        // The hearthstone (presence §2.4, P4: cast for real): when home is on the way, the
        // journey from home plus the cast saves at least HEARTH_GAIN_SEC.
        constexpr uint32 HEARTHSTONE = 6948;
        constexpr uint32 HEARTH_CAST_SEC = 15;      // the 10 s cast and the loading
        constexpr uint32 HEARTH_GAIN_SEC = 60;
        constexpr float HOME_YD = 60.0f;            // within this of its bind point: it is home
        constexpr uint32 HOP_MS = 20 * IN_MILLISECONDS;
        constexpr float HOP_MIN_YD = 100.0f;        // shorter steps wait for the next hop
        constexpr float CLOSE_YD = 60.0f;           // a summon, or he came to it

        // An area's name in the language of `reader`'s client.
        std::string AreaName(uint32 areaId, Player const* reader);
        // It carries a hearthstone that is ready, and has a home on a continent.
        bool HearthReady(Player const* me);
        bool NearHome(Player const* me);
        // Casts the hearthstone as a player does (Shared/ItemUse). True: the cast started.
        bool UseHearth(Player* me);
        // A spot on `watcher`'s map beyond his sight with a walk from it to him (`me` on that
        // map walks it): ground he walked lately (LeaderTrail), else open ground around him,
        // the direction `angle` (radians, from him) first. False: none.
        bool SpotOutOfSight(Player* me, Player* watcher, float angle, WorldLocation& out);

        enum class Step : uint8
        {
            None,       // not travelling (or it ended: he sees it)
            OnRoad,     // keep still, nothing else
            Arrived,    // out of his sight near him: walk the rest
        };

        inline bool IsActive(TravelState const& state) { return state.phase != TravelState::Phase::None; }
        // Could `who` see `me`: the same map and instance, within its visibility distance (+ margin).
        bool InSight(Player const* me, Player const* who);

        void Begin(Player* me, TravelState& state, Player* meet, std::vector<std::string> const& tags,
                   TravelPace const& pace = TravelPace());
        // It came into the world where he can see it (a companion called over, logging in at its
        // place next to him): hidden at once, then out of his sight and walking in like an
        // arrival. The caller hides it before the world shows it (at login).
        void BeginHidden(Player* me, TravelState& state);
        inline bool IsHidden(TravelState const& state)
        {
            return state.phase == TravelState::Phase::Hidden || state.phase == TravelState::Phase::Revealing;
        }
        Step Update(Player* me, TravelState& state, Player* meet, std::vector<std::string> const& tags);
        // Shows the bot again if it was invisible (on the road, hidden).
        void Stop(Player* me, TravelState& state);
    }
}

#endif
