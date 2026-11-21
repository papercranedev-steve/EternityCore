/*
 * Bot daily life (PlayerBots/Life): a bot far from someone it should join travels there on its
 * own, as a player would (design doc .kiro/docs/2026-11-21-companion-presence.md §2.4, user
 * 2026-11-21: "both ends real, the middle simulated"). Used by a companion left behind that no
 * warlock will summon (PartyBotAI; who summons is the Commander's, GatherPlanner).
 *
 *  - Begin: how long the journey takes (Commander/Perception/Spatial/TravelTime::JourneyOf: run,
 *    flight path, boat), clamped to MIN_SEC..MAX_SEC; one party line where it is and how long
 *    ("我在暮色森林，坐鸟过去，大概 4 分钟。").
 *  - On the road: the bot stays where it is (nobody sees it there), says "上船了" halfway when
 *    it crosses the sea. Ends at once when the one it joins can see it (he came by, a summon).
 *  - Arrival: it appears out of his sight and walks the last stretch (the caller's walk,
 *    Commander/Behaviors/Movement/Arrival). Where:
 *      his map is a dungeon: its entrance inside (as a player walks in through the portal);
 *      else on his map, beyond the visibility distance: first ground he walked lately
 *      (LeaderTrail), else open ground around him with a walk to him (from the side the bot
 *      comes from). On another map it first goes there hidden (invisible, next to him), picks
 *      the spot on that map's thread, moves there and shows itself.
 *    Nowhere hidden found: it shows itself where it is (logged).
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
    // Per bot (a member of its AI). Plain data.
    struct TravelState
    {
        enum class Phase : uint8
        {
            None,
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
    };

    namespace Travel
    {
        constexpr uint32 MIN_SEC = 20;
        constexpr uint32 MAX_SEC = 10 * MINUTE;
        constexpr float SIGHT_MARGIN_YD = 25.0f;    // beyond his visibility distance
        constexpr float MAX_WALK_FACTOR = 2.5f;     // the walk in at most this times the straight way
        constexpr uint32 HIDDEN_GIVE_UP_MS = 15 * IN_MILLISECONDS;

        enum class Step : uint8
        {
            None,       // not travelling (or it ended: he sees it)
            OnRoad,     // keep still, nothing else
            Arrived,    // out of his sight near him: walk the rest
        };

        inline bool IsActive(TravelState const& state) { return state.phase != TravelState::Phase::None; }
        // Could `who` see `me`: the same map and instance, within its visibility distance (+ margin).
        bool InSight(Player const* me, Player const* who);

        void Begin(Player* me, TravelState& state, Player* meet, std::vector<std::string> const& tags);
        // It came into the world where he can see it (a companion called over, logging in at its
        // place next to him): hidden at once, then out of his sight and walking in like an
        // arrival. The caller hides it before the world shows it (at login).
        void BeginHidden(Player* me, TravelState& state);
        inline bool IsHidden(TravelState const& state)
        {
            return state.phase == TravelState::Phase::Hidden || state.phase == TravelState::Phase::Revealing;
        }
        Step Update(Player* me, TravelState& state, Player* meet, std::vector<std::string> const& tags);
        // Shows the bot again if it was hidden.
        void Stop(Player* me, TravelState& state);
    }
}

#endif
