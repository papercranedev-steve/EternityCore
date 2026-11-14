/*
 * PartyBot Commander layer 2: shared parts of TargetAssigner (TargetAssigner.h), split by role:
 * TankTargets.cpp (the tank's next enemy) and DamageTargets.cpp (damage dealers).
 *
 * Tank (TankTargets::Choose), first that applies:
 *  1. Rescue (team rescue, 2026-11-07): the loose enemy RescueAssigner gave this tank, in the
 *     same allocation (TeamPlan::duties). Winning it back with threat: that enemy. A taunt:
 *     only when it is out of the taunt's range or sight (the tank walks over; in range
 *     RescueBehavior casts it where the tank stands). benefit = the rescue's net + the switch,
 *     cost = SwitchCost; Urgent when the victim dies within the horizon. Already the tank's
 *     target: kept. Which tank, and taunt or threat or the victim's Fade, is the rescue's
 *     choice (Coordination/Duties/RescueAssigner.h); the loose rule is RescueValue::LooseVictim.
 *  2. Threat (user 2026-11-13, doc .kiro/docs/2026-11-13-tank-threat-priority.md 4): over the
 *     enemies on the tank within TacticalDoctrine::CLUSTER_YD (ThreatRisk::Counts), the one
 *     where one global cooldown of the tank's threat (ThreatGap::FillRate x GCD) takes off the
 *     most risk (Perception/Values/ThreatRisk.h, with the area spell the doctrine counts on,
 *     over DoctrinePlan::RunwayHorizon), net of SwitchCost; the current target pays none. No
 *     risk anywhere: the current target. (Until 2026-11-13: the threat opening's "fill the one
 *     short the most", the top-up by the shortest runway, the Pick tactic's focus step and a
 *     plain "keep".)
 *  3. No target yet: the pull target, else what the leader attacks, else what another member
 *     attacks (WorldModel::targeted first), else the nearest enemy hitting the tank, else the
 *     nearest one, all within JOIN_FIGHT_YD. benefit = its harm over its kill time x the tank's share of
 *     the party's damage (the exchange below against no target).
 * Damage dealers (DamageTargets::Choose):
 *  Threat: an enemy held by a holder (player, tank, tank-like ally; Tanks::IsHolder) needs
 *  ThreatRoom::Enter room to switch onto, ThreatRoom::Stop to stay on (ThreatBehavior stops
 *  hitting below Stop: same rule, hysteresis between the two). An enemy nobody is fighting yet
 *  is the tank's first when there is a tank bot (else the player's: he holds it). No threat
 *  rule on an enemy stamped NoHold (fodder, a trap swarm: TacticalDoctrine.h).
 *  Reach: melee within MELEE_SWITCH_YD of its edge (OUTLIER_MELEE_YD for the party focus when
 *  it is an outlier under the doctrine's KillFirst; JOIN_FIGHT_YD with no target), no idle
 *  creature on the walk (RouteRisk::Blocked, as for the tank); a melee bot's current target
 *  it cannot walk to counts as no target (the melee stance module stops that chase);
 *  ranged within RANGED_SWITCH_YD (JOIN_FIGHT_YD; the ranged
 *  stance module walks it into range and sight). Line of
 *  sight is the cerebellum's (TargetBehavior).
 *  Value (Smith's rule exchange, FocusAssigner.h): killing f before the current target c saves
 *  w_f t_c - w_c t_f (no target: w_f t_f), times the bot's share 1 / damage dealers;
 *  cost = SwitchCost (reach 5 melee, 30 ranged). The party focus if its net is positive (the
 *  party kills one enemy together), else the best positive one, else the current target.
 */

#ifndef MANGOS_COMMANDER_TARGET_RULES_H
#define MANGOS_COMMANDER_TARGET_RULES_H

#include "Commander/Coordination/Duties/FocusAssigner.h"
#include "Commander/Perception/WorldModel.h"
#include <vector>

namespace Commander
{
    struct TeamPlan;

    struct TargetPick
    {
        EnemyFacts const* enemy = nullptr;
        float benefit = 0.0f;
        float cost = 0.0f;
        bool urgent = false;
        bool forced = false;
        char const* reason = "";
    };

    // One Distribute run: candidates and what every rule needs.
    class TargetContext
    {
    public:
        TargetContext(WorldModel const& world, TeamPlan const& plan);

        WorldModel const& world;
        TeamPlan const& plan;
        float tickSec;
        bool tankBot;                   // an alive tank bot that can act
        std::vector<EnemyFacts const*> candidates;

        EnemyFacts const* Find(ObjectGuid guid) const;
        // Hard limits of TargetAssigner.h for `bot`.
        bool Hittable(EnemyFacts const& e, PartyMemberFacts const& bot) const;
        // Intercept primary of `bot` this allocation, empty = none.
        ObjectGuid InterceptOf(ObjectGuid bot) const;
        float InterceptPoints(ObjectGuid bot) const;
        bool InterceptUrgent(ObjectGuid bot) const;     // FocusAssigner::DETAIL_URGENT
        // FocusAssigner::Evaluate, once per enemy and run.
        FocusScore const& Score(EnemyFacts const& e) const;
        // 1 / damage dealers (Smith's rule exchange share).
        float Share() const;

    private:
        struct Pending
        {
            ObjectGuid enemy;
            ObjectGuid controller;
        };
        std::vector<Pending> m_pendingControls;
        mutable std::vector<std::pair<ObjectGuid, FocusScore>> m_scores;
    };

    // A bot with no target joins any fight of ours this close (user, 2026-11-07: whoever can
    // come, comes; was 40 yd). Farther is another fight: the same distance at which an out of
    // combat bot is teleported to the leader (PartyBotAI::UpdateAI). Enemies are on this map
    // only (WorldModelBuilder).
    static constexpr float JOIN_FIGHT_YD = 100.0f;

    namespace TankTargets
    {
        static constexpr float MELEE_REACH = 5.0f;

        // `current`: the bot's target if it is a candidate it may hit, else null.
        TargetPick Choose(TargetContext const& ctx, PartyMemberFacts const& tank, EnemyFacts const* current);
    }

    namespace DamageTargets
    {
        static constexpr float MELEE_SWITCH_YD = 10.0f;
        static constexpr float RANGED_SWITCH_YD = 30.0f;
        // Doctrine Area tactic: a melee bot goes this far for the party focus when it stands
        // outside the pile (an outlier caster); the route check still applies.
        static constexpr float OUTLIER_MELEE_YD = 30.0f;
        static constexpr float MELEE_REACH = 5.0f;
        static constexpr float RANGED_REACH = 30.0f;

        TargetPick Choose(TargetContext const& ctx, PartyMemberFacts const& bot, EnemyFacts const* current);
        // Threat rule above; `staying` = it already is the bot's target.
        bool ThreatAllows(TargetContext const& ctx, PartyMemberFacts const& bot, EnemyFacts const& e, bool staying);
    }
}

#endif
