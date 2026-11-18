/*
 * PartyBot Commander layer 3: casts the control spell the brain gave this bot ("cc" module,
 * crowd control doc .kiro/docs/2026-11-07/2026-11-07-crowd-control.md, todo 10b).
 *
 * Only this module controls: the rotations' own controls (marked targets, the mage's sheep on
 * a second attacker, Blind, Hibernate, Shackle, Banish, Fear, Howl of Terror, Intimidating
 * Shout) are gone since 2026-11-14. Spells not in a ClassKit::Controls list (hunters: Scare
 * Beast, paladins: Turn Evil) are still the rotations'.
 *
 * Propose, for this bot's ControlDuty (TeamPlan::FindControlFor), checked live: the target is
 * alive and not under the same spell any more, in sight, CanTryToCastSpell (range, power,
 * cooldown, form, reagents). Tier Assigned, value = the duty's net points + the global
 * cooldown it costs, cost = that global cooldown (the arbiter compares by net). A control that
 * breaks on damage drops the bot's own attack on its target (BotAction::dropTarget).
 *
 * Opening control of a pull (PullPlan::control, Coordination/Situation/Pull/PullControl.h): in Approach,
 * before the fight, the controller walks to its spot and holds there (MoveTo renewed every
 * tick, Hold once there); the duty comes once the fight started. Casting needs the party to
 * fight (WorldModel::HasPartyEnemy), not the bot itself: the controller is not in combat yet.
 * Sap (beforeOpener, .kiro/docs/2026-11-08-sap.md): Stealth (ClassKit::Stealth) first, sneak
 * to the spot, cast Sap there (target alive, not in combat, in sight, CanTryToCastSpell), then
 * hold until the fight starts; Takeover::Stealth meanwhile (the rogue rotation would cancel
 * the stealth out of combat).
 *
 * Patrol hold (TeamPlan::patrolHold, Coordination/Situation/Patrol/PatrolHold.h; battlefield
 * groups doc 7), all Urgent (the fight is not bearable without it, the doctrine's hold):
 *   cast    the fight is on (an enemy of ours, or the pull past its opener: Return, Settle), the
 *           patrol alive, not under the spell, at least clearYd from fightAt, in range, in sight,
 *           CanTryToCastSpell: cast it, out of combat too;
 *   go      from goAtMs: walk to the first spot with sight of holdAt (eye height), hold there.
 * Before goAtMs nothing (the bot does what it does). Once the patrol is in the fight the
 * doctrine's hold is a duty above.
 *
 * Known limit: CanTryToCastSpell refuses a spell whose aura the target still has, so a sheep
 * is cast again only once the old one ended (not RECAST_LEAD_SEC before).
 */

#ifndef MANGOS_COMMANDER_CROWD_CONTROL_BEHAVIOR_H
#define MANGOS_COMMANDER_CROWD_CONTROL_BEHAVIOR_H

#include "Commander/Behaviors/IBehavior.h"

namespace Commander
{
    struct PartyMemberFacts;

    class CrowdControlBehavior : public IBehavior
    {
    public:
        static constexpr float OPENING_WALK_YD_PER_SEC = 6.0f;

        char const* GetId() const override { return "cc"; }
        bool AppliesTo(TeamBotAI const* bot) const override;
        bool TakesOver(Takeover what, TeamBotAI const* bot, WorldModel const& world, TeamPlan const& plan) const override;
        void Propose(TeamBotAI const* bot, WorldModel const& world,
                     TeamPlan const& plan, std::vector<Proposal>& out) const override;
        // The bot has a control duty while the party fights (a new add hits someone until it is cast).
        bool WantsTickNow(TeamBotAI const* bot, WorldModel const& world, TeamPlan const& plan) const override;

    private:
        // The Sap controller before the pull (PullControlPlan::beforeOpener).
        void ProposeSap(TeamBotAI const* bot, TeamPlan const& plan, PartyMemberFacts const& self,
                        std::vector<Proposal>& out) const;
        // The controller of the pull's patrol hold before the patrol is in the fight
        // (TeamPlan::patrolHold, PatrolHold.h). True = it proposed.
        bool ProposePatrolHold(TeamBotAI const* bot, WorldModel const& world, TeamPlan const& plan,
                               PartyMemberFacts const& self, std::vector<Proposal>& out) const;
    };
}

#endif
