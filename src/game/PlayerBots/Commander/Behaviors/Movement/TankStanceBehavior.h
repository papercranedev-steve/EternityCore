/*
 * PartyBot Commander layer 3 (cerebellum): where a tank stands while the party fights
 * ("tankstance"; tank fight spot doc .kiro/docs/2026-11-14-tank-fight-spot.md, step 1).
 *
 * Before: the original chase (CombatBotBaseAI::BeginChasing, MoveChase in front of the victim)
 * ran after every new target and every enemy that walked off, so the pile drifted wherever the
 * loose enemies went (towards the area caster, out of the Blizzard).
 *
 * Now the module takes the chase over (Takeover::Stance, TeamBotAI::IsChaseControlled:
 * BeginChasing stands down, UpdateAI drops a chase it did not order) while the party fights
 * (WorldModel::InFight), not while the pull places the tank (PullPlan::PlacesTank: the pull
 * walks it, a walk-in pull's chase is the original one) unless the fight spot moves it.
 *
 *  - The fight spot moves this tank (TeamPlan::fightSpot, FightSpotAssigner): once per move,
 *    the plan's spots in order, the first whose ground is found within MAX_STEP_Z of the tank,
 *    that no idle creature covers, and whose path is safe (SafeRoute::PathIsSafe,
 *    MAX_PATH_DETOUR) is written to fightSpot.chosen (-2: none). Then walk there (tier
 *    Assigned, valued with the plan's benefit and cost): backwards facing the tank's victim
 *    (Backpedal, order renewed every tick, the rotation keeps fighting), or MoveTo when no
 *    backwards path is found or the tank has no victim, with Hold while that walk is under way.
 *    (Was PatrolBehavior's until 2026-11-14.)
 *  - Hiding from a caster (TeamPlan::HidingFor): nothing, LureBehavior walks it.
 *  - Otherwise the tank stays where it stands. Its enemy (the brain's target, else the victim)
 *    out of melee reach:
 *      comes by itself (it hits this tank, walks in to melee: Tags::Engage::Melee with melee
 *      on, not rooted, controlled, casting or running away): no chase; a chase under way is stopped
 *      (StopChase);
 *      else (stands back casting, rooted, hits someone else and the tank must get it with a
 *      melee ability, runs away): chase it (BotActionType::Chase, angle 0 in front, value
 *      LostAction(min(HORIZON_SEC, the fight's remaining time)), tier Assigned; an order without
 *      value when the plan sends this tank to wake it, TeamPlan::controlEnd: with only controlled
 *      enemies left LostAction is 0 and the chase was dropped). Whether to taunt
 *      it instead is the team rescue's (RescueAssigner); once it turns, it comes.
 *    Where the chase ended is where the tank stands: the next refresh judges the spot again
 *    from there (FightSpot), it does not walk back on its own (doc 9, answer 4).
 *  - Before all that (not while the pull or the fight spot walks it, not while hiding): an
 *    enemy whose victim is this tank has found no way to it for UNREACHED_MS
 *    (EnemyFacts::unreachedMs: the tank stands in a wall, behind bars, off the navmesh; the
 *    core then makes it evade every hit and go home after 24 s, stockade-20261125-221049: the
 *    tank stepped 2 yd while chasing another enemy, stood 4 yd off it for 25 s, it reset).
 *    Walk to where it stands (MoveTo, tier Urgent: its fight is lost otherwise; the nearest such
 *    enemy). It stands still meanwhile; once it reaches the tank the core's timer and the walk end.
 *  - Then: out of everyone's sight, in a wall (strat-20261126-232120: the line of sight pull put
 *    the tank inside a small object of Stratholme's street; the enemies hit it, its own spells
 *    failed for sight, the healer found no spot that saw it and the tank died after 33 s). An
 *    enemy hitting this tank is in its melee reach but out of its sight, and no bot healing the
 *    team (PartyMemberFacts::HealsTeam) sees it: walk along the path to the nearest such healer
 *    to the first of SIGHT_STEPS yards that the healer sees (SpotOk: ground, no idle creature,
 *    safe path; MoveTo, tier Urgent). The tank does not count on the healer to find it (rule
 *    "the other duties get it wrong"). Not while hiding from a caster (above). Without a healer
 *    bot nothing.
 *
 * Not while: the retreat runs, rooted, on a transport, the bot cannot act.
 * Tank facing (turning enemies away from the party) is not done.
 */

#ifndef MANGOS_COMMANDER_TANK_STANCE_BEHAVIOR_H
#define MANGOS_COMMANDER_TANK_STANCE_BEHAVIOR_H

#include "Commander/Behaviors/IBehavior.h"

namespace Commander
{
    class TankStanceBehavior : public IBehavior
    {
    public:
        static constexpr float MAX_STEP_Z = 6.0f;
        static constexpr float MAX_PATH_DETOUR = 1.8f;
        static constexpr float PATH_FACTOR = 1.3f;
        static constexpr uint32 HOLD_EXTRA_MS = 1000;
        static constexpr uint32 BACKPEDAL_ORDER_MS = 1500;
        static constexpr float HORIZON_SEC = 8.0f;
        // An enemy hitting this tank has found no way to it this long (EnemyFacts::unreachedMs;
        // the core makes it evade at 3 s): walk to it.
        static constexpr uint32 UNREACHED_MS = 1000;
        // Out of everyone's sight: the points along the path to the healer tried, nearest first.
        static constexpr float SIGHT_STEPS[] = { 3.0f, 5.0f, 7.0f, 10.0f };

        char const* GetId() const override { return "tankstance"; }
        bool AppliesTo(TeamBotAI const* bot) const override;
        bool TakesOver(Takeover what, TeamBotAI const* bot, WorldModel const& world, TeamPlan const& plan) const override;
        void Propose(TeamBotAI const* bot, WorldModel const& world,
                     TeamPlan const& plan, std::vector<Proposal>& out) const override;
    };
}

#endif
