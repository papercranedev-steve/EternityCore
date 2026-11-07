/*
 * PartyBot Commander layer 2 (brain): an idle patrol walks into the fight before it ends.
 * From strat-20261101-225111: Patchwork Horror (17000 hp elite, ~70 s loop) walked through the
 * fight spot 28 s into a 7-elite pull and the party wiped.
 *
 * Pure (facts and plan only). Per refresh while the party fights and no move is running:
 *   zone       = patrol aggro range against the tank (PatrolFacts::reach) + ZONE_MARGIN
 *                around every living member
 *   arrive     = first point of the patrol's predicted walk (PatrolFacts::track) inside the
 *                zone; ignored when the fight should end first (Value::FightRemainingSec)
 *   benefit    = its melee on the tank (after armor) from arrival until the end of the fight
 *                plus its own kill time, + Value::FightExtension(its kill time)
 *   spots      = rings of SPOT_DISTANCES around the tank where the zone of the party, moved
 *                along with the tank, is never entered by the predicted walk (as far as the
 *                track goes), nearest first, at most MAX_SPOTS
 *   cost       = Value::FightExtension(walk time to the nearest spot): the fight lasts longer
 *                while the tank walks and the enemies follow
 * benefit > cost and a spot: TeamPlan::patrolMove (PatrolBehavior walks the tank there).
 * Otherwise the party stays and the leader is told why. Each patrol is judged once per fight.
 * While the pull still places the tank (Settle with the arrival wait or an Active line of sight
 * pull, whose point already avoids patrol paths) a patrol arriving after
 * PULL_PLACING_DEFER_SEC is judged once that is over.
 *
 * Every other bot (healer, ranged, melee) is a follower (TeamPlan.h, PatrolFollower) and walks
 * the tank's shift itself (PatrolBehavior).
 *
 * Move phase ends when the tank and the followers arrived or after MOVE_TIMEOUT_MS; then
 * Guard (strat-20261102-225354: after the move the tank walked back to win two casters back
 * that stood at the old spot with the rogue, and the patrol joined there):
 *   avoid  = every refresh while active, enemies the patrol's walk reaches (InPath) before the
 *            fight ends. Nobody chases or walks over to them, they are not the focus; in
 *            Guard bots switch off them (TeamPlan.h, PatrolMovePlan::avoid).
 *   lures  = Guard only. A caster of `avoid` standing back (melee off, or a main ranged
 *            spell, more than LURE_MIN_DISTANCE from its victim) casting at a bot: that bot
 *            hides from it (PatrolLure; the point: PatrolPlanner::Step, LosBreak.h) and keeps
 *            fighting what it reaches there. Done when the casters came out of the path,
 *            within LURE_ARRIVED of the hider, turned to someone else or are gone; Failed after
 *            LURE_TIMEOUT_MS. One lure per hider and fight, at most MAX_LURE_CASTERS casters.
 *   kill   = casters of `avoid` standing back that no lure brings out (no point, failed,
 *            victim is a player or a pet, hider already used): a ranged damage dealer kills
 *            each (FocusAssigner intercept).
 * Guard ends when the old fight spot is clear of the patrol's walk for the rest of the fight
 * and no enemy stands in its path, or after GUARD_TIMEOUT_MS.
 * The plan ends at once when no spot passed the live checks, the patrol joined the fight or
 * left the scan, the tank changed, or the retreat or a pull took over.
 *
 * Known limits: the track covers at most WorldModelBuilder::PATROL_FIGHT_HORIZON_SEC; the spot
 * geometry (ground, idle creatures, path) is only checked by the behavior; facts have no
 * "moving" flag, so a caster already walking over may get a lure it does not need.
 */

#ifndef MANGOS_COMMANDER_PATROL_ASSIGNER_H
#define MANGOS_COMMANDER_PATROL_ASSIGNER_H

#include "Commander/Coordination/IAssigner.h"

namespace Commander
{
    struct PartyMemberFacts;
    struct PatrolFacts;
    struct PatrolMovePlan;
    struct Position3;

    class PatrolAssigner : public IAssigner
    {
    public:
        static constexpr float ZONE_MARGIN = 5.0f;
        static constexpr float SPOT_DISTANCES[] = { 15.0f, 25.0f, 35.0f };
        static constexpr uint32 SPOT_ANGLES = 12;
        static constexpr uint32 MAX_SPOTS = 8;
        static constexpr float RUN_YARDS_PER_SEC = 7.0f;
        static constexpr float PATH_FACTOR = 1.3f;
        static constexpr float ARRIVED_DISTANCE = 4.0f;
        static constexpr uint32 MOVE_TIMEOUT_MS = 15 * 1000;
        static constexpr uint32 GUARD_TIMEOUT_MS = 60 * 1000;
        // Lures (same numbers as the line of sight pull, PullPlanner / PullAssigner).
        static constexpr float LURE_MIN_DISTANCE = 10.0f;
        static constexpr float LURE_ARRIVED = 8.0f;
        static constexpr uint32 LURE_TIMEOUT_MS = 8 * 1000;
        static constexpr uint32 MAX_LURE_CASTERS = 2;
        // While the pull still places the tank (arrival wait, line of sight pull), a patrol
        // arriving later than this is judged afterwards: the line of sight break lasts at most
        // PullAssigner::LOS_BREAK_TIMEOUT_MS (8 s), plus a move of about 15 yd.
        static constexpr float PULL_PLACING_DEFER_SEC = 20.0f;

        char const* GetId() const override { return "patrol"; }
        void Assign(WorldModel const& world, TeamPlan& plan) const override;
        void Clear(TeamPlan& plan) const override;

        // Zone radius around the patrol's walk: its aggro range against the tank + ZONE_MARGIN.
        static float ZoneRadius(PatrolFacts const& p);
        // The patrol's predicted walk comes within ZoneRadius of `pos` in less than `withinSec`.
        static bool InPath(PatrolFacts const& p, Position3 const& pos, float withinSec);
        // The plan's patrol among the facts, null when it joined the fight or left the scan.
        static PatrolFacts const* FindPatrol(WorldModel const& world, PatrolMovePlan const& m);

    private:
        void Continue(WorldModel const& world, TeamPlan& plan, PartyMemberFacts const& tank) const;
    };
}

#endif
