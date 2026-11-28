/*
 * PartyBot Commander layer 3 (cerebellum): follow the leader like a person (user, 2026-11-15).
 *
 * Before: every bot reacted in the same 100 ms check of FollowMovementGenerator, kept a fixed
 * offset at the leader's facing (a leader turning in place swung all of them around him),
 * walked to the exact point and turned to exactly the leader's facing at the end.
 *
 * Each bot gets a personality, fixed per character (from its guid, so it stays the same across
 * logins and differs between bots):
 *   - a preferred offset DIST_MIN..DIST_MAX behind the leader, SIDE_MIN..SIDE_MAX to its own
 *     side (left or right);
 *   - a reaction delay before a standing bot starts after the leader;
 *   - a band: it starts only when the leader is START_SLACK farther than its distance and,
 *     once the leader stops, stops wherever it is within STOP_SLACK of it;
 *   - attentive (turns to the leader once it leaves ATTENTIVE_ARC) or lazy (only when the
 *     leader is behind, LAZY_ARC), after a turn delay.
 * The offset is measured from the leader's direction of travel (FollowStyle.h, generator side).
 * Place (user 2026-11-21: the spacing after a stop was the same every time, the place came only
 * from the guid): the personality's offset is only the bot's preferred part of the zone behind
 * the leader. Each start of the follow and each stop of the leader after REROLL_MOVE_YD picks a
 * new place in it (Reroll), at least MIN_GAP from where the team's other members follow to or
 * stand: dart throwing, PLACE_TRIES candidates (a Poisson disk built one bot at a time), else
 * the candidate farthest from the others. The zone grows with the followers (AREA_PER_FOLLOWER).
 * Lead (user 2026-11-25): the team's main tank (Tanks::Main, a bot, not the leader itself)
 * takes the same zone in front of the leader instead, and starts at once when the leader walks
 * towards it (LEAD_* style, FollowMovementGenerator's ahead rule). FollowSpot's candidates are
 * behind first: where the front is not safe the tank goes behind and stays there (no drift
 * back) until the next stop rerolls a place in front.
 * Tick: while walking the bot's place drifts a little; once the leader has stood a while the
 * bot looks around, takes a step or sits down (out of combat only). What he does
 * (Perception/Profiles/PlayerActivity, player pacing doc P3): busy where he stands or away, the
 * idle acts start sooner; standing a while, the first act spreads the bot out (the loose zone);
 * away, most bots sit down. A place outside the bot's
 * zone (FollowSpot moved it for safety) does not drift: the next stop rerolls.
 * FollowSpot still decides whether the offset is safe: TeamBotAI asks it with Heading() after
 * Reroll() / Tick(); the first move of each MoveFollow is never held back.
 *
 * Map thread of the bot.
 */

#ifndef MANGOS_COMMANDER_HUMAN_FOLLOW_H
#define MANGOS_COMMANDER_HUMAN_FOLLOW_H

#include "Common.h"
#include "FollowStyle.h"
#include "Commander/Behaviors/Movement/FollowSpot.h"
#include "Commander/Perception/Spatial/SealedRoom.h"

class Player;
class Unit;

namespace Commander
{
namespace HumanFollow
{
    constexpr float DIST_MIN = 2.5f;
    constexpr float DIST_MAX = 5.5f;
    constexpr float SIDE_MIN = 0.2f;              // radians off straight behind
    constexpr float SIDE_MAX = 1.1f;
    constexpr uint32 REACTION_MIN_MS = 200;
    constexpr uint32 REACTION_MAX_MS = 650;
    constexpr float START_SLACK_MIN = 1.5f;
    constexpr float START_SLACK_MAX = 4.0f;
    constexpr float STOP_SLACK_MIN = 0.3f;
    constexpr float STOP_SLACK_MAX = 2.0f;
    constexpr float BAND_GAP = 0.5f;              // stopSlack at most startSlack - this
    constexpr float ATTENTIVE_SHARE = 0.6f;
    constexpr float ATTENTIVE_ARC = 1.2f;         // radians, total width
    constexpr float LAZY_ARC = 3.0f;
    constexpr uint32 TURN_DELAY_MIN_MS = 300;
    constexpr uint32 TURN_DELAY_MAX_MS = 1500;

    // Place: the zone is ZONE_ARC wide behind the leader, DIST_MIN deep to DIST_MAX or deeper
    // with more followers (AREA_PER_FOLLOWER each). Depth is a share 0..1 of the zone's area.
    // Reroll: the personality's share +-PLACE_SPREAD and angle +-PLACE_ANGLE.
    constexpr float MIN_GAP = 1.5f;
    constexpr uint32 PLACE_TRIES = 12;
    constexpr float ZONE_ARC = 3.0f;              // radians, total width
    constexpr float AREA_PER_FOLLOWER = 3.5f;     // square yards
    constexpr float PLACE_SPREAD = 0.35f;
    constexpr float PLACE_ANGLE = 0.5f;
    // A new place once the leader stops this far from where it stood before (not for a step).
    constexpr float REROLL_MOVE_YD = 4.0f;
    // Lead: the tank in front starts before the leader walks into it (leader after the slack
    // and the reaction: 1.5 + 0.2 s x 7 yd/s = 2.9 yd, the tank stands 2.5 yd + both radii ahead).
    constexpr uint32 LEAD_REACTION_MS = REACTION_MIN_MS;
    constexpr float LEAD_START_SLACK = START_SLACK_MIN;

    // Drift (leader walking): every DRIFT_EVERY_MIN..MAX_MS a new place DRIFT_SPREAD /
    // DRIFT_ANGLE around the current one (MIN_GAP kept; the next stop rerolls near the bot's
    // preferred part again, also after FollowSpot moved it for safety).
    constexpr uint32 DRIFT_EVERY_MIN_MS = 6000;
    constexpr uint32 DRIFT_EVERY_MAX_MS = 15000;
    constexpr float DRIFT_SPREAD = 0.12f;
    constexpr float DRIFT_ANGLE = 0.4f;
    // Idle (leader standing IDLE_AFTER_MIN..MAX_MS, per bot): every IDLE_EVERY_MIN..MAX_MS one
    // of: look around (LOOK_TURN either way), a step (a drift), sit down after SIT_AFTER_MS
    // (the generator stands the bot up when it starts after the leader).
    constexpr uint32 IDLE_AFTER_MIN_MS = 6000;
    constexpr uint32 IDLE_AFTER_MAX_MS = 15000;
    constexpr uint32 IDLE_EVERY_MIN_MS = 4000;
    constexpr uint32 IDLE_EVERY_MAX_MS = 12000;
    constexpr float LOOK_TURN = 1.3f;
    constexpr uint32 SIT_AFTER_MS = 25000;
    constexpr uint32 LOOK_CHANCE = 45;            // percent; then STEP_CHANCE, the rest sit
    constexpr uint32 STEP_CHANCE = 30;
    // The leader counts as standing while it stays within this of where it stopped.
    constexpr float STILL_YD = 0.5f;
    // The leader sits down (to eat, to rest, on a whim; player-centric doc block 6, user
    // 2026-11-28): SIT_WITH_PCT of the bots sit too after SIT_WITH_MIN..MAX_MS each, and get up
    // STAND_WITH_MIN..MAX_MS after he stands (walking off: the follow stands it up at once).
    constexpr uint32 SIT_WITH_PCT = 70;
    constexpr uint32 SIT_WITH_MIN_MS = 1200, SIT_WITH_MAX_MS = 4000;
    constexpr uint32 STAND_WITH_MIN_MS = 400, STAND_WITH_MAX_MS = 1500;
    // He is away (his AFK flag, PlayerActivity; player pacing doc P3): AFK_SIT_PCT of the bots
    // sit after AFK_SIT_MIN..MAX_MS each and get up after him as after his sit; the others go
    // on with their idle acts.
    constexpr uint32 AFK_SIT_PCT = 80;
    constexpr uint32 AFK_SIT_MIN_MS = 3000, AFK_SIT_MAX_MS = 12000;
    // He is busy where he stands or away (PlayerActivity::Waits: loot, an NPC, a trade, a cast,
    // eating): the idle acts start after at most BUSY_IDLE_AFTER_MS, he is not walking on.
    constexpr uint32 BUSY_IDLE_AFTER_MS = 2500;
    // Spread (player pacing doc P3): once he has stood SPREAD_AFTER_MS (waiting:
    // BUSY_SPREAD_AFTER_MS), a bot's next idle act is a new place in the loose zone, LOOSE_ARC
    // wide, LOOSE_AREA_PER_FOLLOWER per follower, at least LOOSE_DIST_MAX deep; its steps stay
    // in it. The lead stays behind him while he deals with an NPC (not between them). The next
    // stop after a walk rerolls in the close zone.
    constexpr uint32 SPREAD_AFTER_MS = 12000;
    constexpr uint32 BUSY_SPREAD_AFTER_MS = 4000;
    // Mount with him a moment after he is up (the lead sooner), off a moment after he gets down
    // (at once when someone fights). Short: he rides off at once and the bot must keep up.
    constexpr uint32 MOUNT_WITH_MIN_MS = 300, MOUNT_WITH_MAX_MS = 900;
    constexpr uint32 LEAD_MOUNT_MIN_MS = 150, LEAD_MOUNT_MAX_MS = 400;
    constexpr uint32 DISMOUNT_WITH_MIN_MS = 200, DISMOUNT_WITH_MAX_MS = 800;
    constexpr float LOOSE_ARC = 4.4f;
    constexpr float LOOSE_AREA_PER_FOLLOWER = 8.0f;
    constexpr float LOOSE_DIST_MAX = 7.0f;

    // Close (user 2026-11-28): the leader within SealedRoom::CLOSE_FOLLOW_YD of a boss door that
    // shuts when the boss starts (BossPrep::seal; Baron Rivendare's gate shut with the bots
    // outside, rivendare-20261128-190203). Every bot follows CLOSE_DIST_MIN..MAX right behind
    // him, its side the personality's scaled to CLOSE_SIDE_MAX; no lead in front (the tank
    // would step through first), no drift, idle acts, sitting or spreading; it starts at once
    // (CLOSE_* style). A leader who walks in has them through with him before the door shuts
    // (the boss's first pulse comes 0-2 s after he is in reach). The leader past the door:
    // the places are in front of him towards the boss (behind him may still be outside), and
    // FollowSpot does not move them back (InSealedRoom). A new close place only when the wanted
    // one moved CLOSE_REISSUE_YD.
    constexpr float CLOSE_DIST_MIN = 1.5f;
    constexpr float CLOSE_DIST_MAX = 2.5f;
    constexpr float CLOSE_SIDE_MAX = 0.4f;          // radians
    constexpr uint32 CLOSE_REACTION_MS = 100;
    constexpr float CLOSE_START_SLACK = 0.8f;
    constexpr float CLOSE_STOP_SLACK = 0.3f;
    constexpr float CLOSE_REISSUE_YD = 0.75f;

    struct Personality
    {
        FollowSpot::Offset offset;
        uint32 reactionMs = 0;
        float startSlack = 0.0f;
        float stopSlack = 0.0f;
        float faceArc = 0.0f;
        uint32 turnDelayMs = 0;
        uint32 idleAfterMs = 0;
    };

    // Per bot (TeamBotAI member). Plain data.
    struct IdleState
    {
        uint32 nextActMs = 0;       // 0: not scheduled
        bool leaderStill = false;
        uint32 stillSinceMs = 0;
        float stillX = 0.0f;
        float stillY = 0.0f;
        bool sat = false;           // sat down by Tick (stood up again on combat)
        // The leader sits: whether this bot sits with him this time (rolled once per sit), when;
        // it sat because he did, and gets up after him (standWithAtMs).
        bool sitRolled = false;
        bool sitsWith = false;
        uint32 sitWithAtMs = 0;
        bool satWithLeader = false;
        uint32 standWithAtMs = 0;
        // Spread out while he stands (a place in the loose zone), around which side (front or
        // back: CenterOf).
        bool spread = false;
        float spreadCenter = 0.0f;
        // Following close at a sealed room's door (CLOSE_*), at this place.
        bool close = false;
        FollowSpot::Offset closeWant;
        bool stopKnown = false;     // where the leader last stood (REROLL_MOVE_YD)
        float stopX = 0.0f;
        float stopY = 0.0f;
    };

    Personality Of(Player const* bot);

    // `bot` walks in front of `leader`: the main tank of its team, not the leader (not at a
    // sealed room's door: CLOSE_*).
    bool Leads(Player const* bot, Unit const* leader);

    // The sealed room whose door `leader` is near (CLOSE_*; TeamPlan::bossPreps of the bot's
    // commander, out of the fight only), or false.
    bool NearSealedRoom(Player const* bot, Unit const* leader, SealedRoom& out);
    // `leader` is past the door of that room: FollowSpot must not move the close place back
    // behind him (outside the door).
    bool InSealedRoom(Player const* bot, Unit const* leader);

    // A new place in the bot's preferred part of its zone (behind `leader`, in front for the
    // lead), MIN_GAP from the team's other members where possible (FollowSpot not asked: the
    // caller does).
    FollowSpot::Offset Reroll(Player const* bot, Unit const* leader, float heading);

    // The direction the follow offset is measured from: the heading of the bot's styled follow
    // of `leader`, else the leader's facing (a new follow starts from it).
    float Heading(Player const* bot, Unit const* leader);

    FollowStyle StyleOf(Player const* bot, Unit const* leader, float heading);

    // Every bot update with a leader; `following`: it follows `leader` with a style at `current`
    // (after the follow spot check). Looks around and sits by itself; true: follow at `next`
    // instead (a reroll at a stop, a drift or a step; the caller checks it with FollowSpot).
    // Not following or in combat: nothing, and a bot it sat down stands up.
    bool Tick(Player* bot, Unit const* leader, IdleState& state, bool following,
              FollowSpot::Offset const& current, FollowSpot::Offset& next);
}
}

#endif
