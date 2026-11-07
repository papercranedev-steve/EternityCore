/*
 * PartyBot Commander layer 1 (eyes): "would this idle creature notice a stealthed rogue here".
 * For the walk to a Sap spot (Perception/SapSpot.h, Sap doc .kiro/docs/2026-11-08-sap.md 2, 4).
 *
 * The server's rule (Unit::CanDetectStealthOf, Unit.cpp, with sniffed classic distances):
 *   detect = 5/6 + (creature level x 5 - stealth) x (5/6) / 5   (x 2 per level when the
 *            creature is more than 3 levels above the rogue), clamped to 0..30 yd
 *   stealth = the Stealth aura's SPELL_AURA_MOD_STEALTH amount + SPELL_AURA_MOD_STEALTH_LEVEL
 *            auras (talents); before the rogue stealths: the Stealth spell's amount
 *   behind the creature (not in its front 180 degrees): detect - 9
 *   always noticed within COLLISION_YD; never beyond the creatures' configured maximum;
 *   a stunned creature notices nothing.
 *   Alert (GridNotifiersImpl, CreatureAI::TriggerAlertDirect): within detect + ALERT_YD the
 *   creature stops and turns to face the rogue for 5 s, so its front distance applies then.
 * So a point is safe from a creature when its distance is more than
 *   front                         the point is in front of it, or it walks around (random
 *                                 wanderer: from anywhere in its circle; patrol: it faces
 *                                 its way, counted as front)
 *   min(behind + ALERT_YD, front) behind it (an alert would turn it)
 * at least COLLISION_YD, plus MARGIN_YD for the bot's last step. Line of sight is not asked
 * (conservative: the creature's aggro needs it, the detection does not).
 *
 * Reads live creatures: map thread.
 */

#ifndef MANGOS_COMMANDER_STEALTH_DETECT_H
#define MANGOS_COMMANDER_STEALTH_DETECT_H

#include "Commander/Perception/WorldModel.h"
#include <vector>

class Creature;
class Player;
class SpellEntry;

namespace Commander
{
namespace StealthDetect
{
    float const COLLISION_YD = 1.5f;
    float const BEHIND_PENALTY_YD = 9.0f;
    float const ALERT_YD = 5.0f;
    float const MAX_DETECT_YD = 30.0f;
    float const MARGIN_YD = 1.0f;
    // The walk is checked every this many yards.
    float const PATH_SAMPLE_STEP = 2.0f;

    // The rogue's stealth value: its active Stealth aura, else `stealthSpell`'s amount (0 when
    // it has neither), plus SPELL_AURA_MOD_STEALTH_LEVEL auras.
    int32 StealthSkill(Player const* rogue, SpellEntry const* stealthSpell);

    // One idle creature as the stealth check sees it.
    struct Watcher
    {
        ObjectGuid guid;
        Position3 pos;
        float orientation = 0.0f;
        float front = 0.0f;         // detection distance in front of it (yd, centre to centre)
        float behind = 0.0f;        // front - BEHIND_PENALTY_YD, may be negative (never noticed there)
        Position3 home;             // random wanderer: its circle
        float wander = 0.0f;
        bool turns = false;         // wanders or patrols: front everywhere
        bool blind = false;         // stunned: notices nothing
        float maxRange = MAX_DETECT_YD;
    };

    Watcher WatcherOf(Creature const* c, Player const* rogue, int32 stealthSkill);

    // Least distance from `w` that `at` must keep (header rule, margin included).
    float SafeDistance(Watcher const& w, Position3 const& at);
    bool Notices(Watcher const& w, Position3 const& at);

    // Idle creatures that would attack the rogue (AggroRange::IsIdleAggressor) within
    // `scanRadius` of `center`.
    class Watchers
    {
    public:
        Watchers(Player* rogue, Position3 const& center, float scanRadius, int32 stealthSkill);
        // Some creature notices the rogue standing at `at`.
        bool Notice(Position3 const& at) const;
        // The navmesh walk from the rogue to `to`: reachable (SafeRoute::PathReaches), at most
        // `maxDetour` times the straight line (+2 yd), no sample noticed. `length`: the path's.
        bool WalkIsSafe(Player* rogue, Position3 const& to, float maxDetour, float* length = nullptr) const;
        std::vector<Watcher> const& All() const { return m_watchers; }

    private:
        std::vector<Watcher> m_watchers;
    };
}
}

#endif
