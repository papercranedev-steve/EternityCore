/*
 * Bot daily life (PlayerBots/Life), errands: standing at a quest giver as a player does, for the
 * quest errands (QuestTurnIn.h, QuestPickUp.h). The bot walks to its own spot in a small arc in
 * front of the giver, on the leader's side and beside him (party bots do not stack on one
 * point), stops, faces the giver and talks a moment (HOLD_MS) before the errand hands in or
 * takes the quest; afterwards it stays facing the giver a little (LINGER_MS) before it follows
 * again. A second quest at the same giver right after is quicker (AGAIN_HOLD_MS).
 *
 * Quests the leader has too are done with him: only at a giver he stands by (TOGETHER_YD), so
 * the party hands in and takes the next one together, not a bot running off on its own.
 *
 * Map thread of the bot.
 */

#ifndef MANGOS_LIFE_GIVER_VISIT_H
#define MANGOS_LIFE_GIVER_VISIT_H

#include "Common.h"
#include "ObjectGuid.h"

class Player;
class WorldObject;

namespace Life
{
    // Per errand (a member of its state). Plain data.
    struct GiverVisitState
    {
        ObjectGuid giver;           // of this visit, empty = none
        bool spotSet = false;
        float spotX = 0.0f, spotY = 0.0f, spotZ = 0.0f;
        bool direct = false;        // the spot was not in reach: walking to the giver itself
        uint32 walkSinceMs = 0;
        uint32 nextMoveMs = 0;
        uint32 arrivedMs = 0;       // standing at the giver since, 0 = walking
        uint32 lingerUntilMs = 0;   // after the errand's act: stays facing the giver until then
        ObjectGuid lastGiver;       // the giver of the last act, and when
        uint32 lastActMs = 0;
    };

    namespace GiverVisit
    {
        constexpr float TOGETHER_YD = 12.0f;
        constexpr float SPOT_YD = 3.0f;
        constexpr float SPOT_REACHED_YD = 1.2f;
        constexpr uint32 REPATH_MS = 2000;
        // In reach but not on the spot this long (something in the way): it stands where it is.
        constexpr uint32 SPOT_GIVE_UP_MS = 4000;
        constexpr uint32 HOLD_MS = 1500;
        constexpr uint32 AGAIN_HOLD_MS = 500;
        constexpr uint32 AGAIN_WINDOW_MS = 5000;
        constexpr uint32 LINGER_MS = 1500;

        enum class Step
        {
            Walking,    // on the way (the errand is busy)
            Holding,    // standing, facing, talking (busy)
            Act,        // held long enough: the errand acts now, then calls Acted
        };

        // In reach of the giver to talk to it (creature: interaction distance, object: its own).
        bool InReach(Player const* me, WorldObject const* giver);

        // The leader stands by the giver (quests done together with him).
        bool LeaderBy(Player const* leader, WorldObject const* giver);

        // One tick of the visit to `giver` (a new giver starts a new visit). `leader`: whose side
        // the arc is on (null: the bot's own side).
        Step Approach(Player* me, GiverVisitState& state, WorldObject* giver, Player const* leader, uint32 nowMs);

        // The errand acted at the giver: the linger starts.
        void Acted(GiverVisitState& state, uint32 nowMs);

        // True while the bot stays facing the giver after its act (busy).
        bool Lingering(Player* me, GiverVisitState& state, uint32 nowMs);

        // The visit ends (gave up, done): back to following. Keeps the last act.
        void End(GiverVisitState& state);
    }
}

#endif
