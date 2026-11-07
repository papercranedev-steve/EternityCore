/*
 * PartyBot Commander layer 2: shared parts of TargetAssigner (TargetAssigner.h), split by role:
 * TankTargets.cpp (the tank's next enemy) and DamageTargets.cpp (damage dealers).
 *
 * Tank (TankTargets::Choose), first that applies:
 *  1. Loose enemy: hitting a member that is not a tank (not an ally, not a charmed member),
 *     within TANK_REACH yd, no other tank bot nearer by TANK_SHARE_MARGIN, a straight walk
 *     that does not cross an idle creature (Perception/RouteRisk.h).
 *     benefit = (loss doing nothing - loss until won back with threat) x override weight
 *     (Perception/LooseEnemy.h), cost = SwitchCost; the best positive one. Urgent when the
 *     victim dies within the horizon. Already the tank's target: kept.
 *  2. Top up: an enemy on the tank whose lead (ThreatView::VictimLead / its threat) is below
 *     LEAD_SWITCH while the current target's is above LEAD_KEEP. benefit = what the runner-up
 *     would take for RECLAIM_SEC (LooseEnemy::Loss) x FRAGILE_WEIGHT (chance the thin lead
 *     breaks), cost = SwitchCost.
 *  3. Keep the current target.
 *  4. No target yet: the pull target, else what the leader attacks, else what another member
 *     attacks (WorldModel::targeted first), else the nearest enemy hitting the tank, else the
 *     nearest one, all within JOIN_FIGHT_YD. benefit = its harm over its kill time x the tank's share of
 *     the party's damage (the exchange below against no target).
 * Damage dealers (DamageTargets::Choose):
 *  Threat: an enemy held by a holder (player, tank, tank-like ally; Tanks::IsHolder) needs
 *  ThreatRoom::Enter room to switch onto, ThreatRoom::Stop to stay on (ThreatBehavior stops
 *  hitting below Stop: same rule, hysteresis between the two). An enemy nobody is fighting yet
 *  is the tank's first when there is a tank bot (else the player's: he holds it).
 *  Reach: melee within MELEE_SWITCH_YD of its edge (JOIN_FIGHT_YD with no target), no idle
 *  creature on the straight walk; ranged within RANGED_SWITCH_YD (JOIN_FIGHT_YD; the ranged
 *  stance module walks it into range and sight). Line of
 *  sight is the cerebellum's (TargetBehavior).
 *  Value (Smith's rule exchange, FocusAssigner.h): killing f before the current target c saves
 *  w_f t_c - w_c t_f (no target: w_f t_f), times the bot's share 1 / damage dealers;
 *  cost = SwitchCost (reach 5 melee, 30 ranged). The party focus if its net is positive (the
 *  party kills one enemy together), else the best positive one, else the current target.
 */

#ifndef MANGOS_COMMANDER_TARGET_RULES_H
#define MANGOS_COMMANDER_TARGET_RULES_H

#include "Commander/Coordination/FocusAssigner.h"
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
        static constexpr float TANK_REACH = 40.0f;
        static constexpr float TANK_SHARE_MARGIN = 2.0f;
        static constexpr float LEAD_SWITCH = 0.15f;
        static constexpr float LEAD_KEEP = 0.30f;
        static constexpr float RECLAIM_SEC = 2.0f;
        static constexpr float FRAGILE_WEIGHT = 0.5f;
        static constexpr float MELEE_REACH = 5.0f;

        // `current`: the bot's target if it is a candidate it may hit, else null.
        TargetPick Choose(TargetContext const& ctx, PartyMemberFacts const& tank, EnemyFacts const* current);
    }

    namespace DamageTargets
    {
        static constexpr float MELEE_SWITCH_YD = 10.0f;
        static constexpr float RANGED_SWITCH_YD = 30.0f;
        static constexpr float MELEE_REACH = 5.0f;
        static constexpr float RANGED_REACH = 30.0f;

        TargetPick Choose(TargetContext const& ctx, PartyMemberFacts const& bot, EnemyFacts const* current);
        // Threat rule above; `staying` = it already is the bot's target.
        bool ThreatAllows(TargetContext const& ctx, PartyMemberFacts const& bot, EnemyFacts const& e, bool staying);
    }
}

#endif
