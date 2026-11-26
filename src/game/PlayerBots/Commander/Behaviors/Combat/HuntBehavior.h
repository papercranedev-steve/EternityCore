/*
 * PartyBot Commander layer 3 (cerebellum): attack the creature the brain chose for a team of
 * bots that finds its own fights (TeamPlan::hunt, Coordination/Situation/Hunt/HuntAssigner.h).
 * Out of combat, not resting, the hunt target not the victim yet: `Attack` it (Assigned).
 * Then TeamBotAI::FightVictim walks a melee bot in / a caster into range and runs the class
 * rotation as the opener (TeamBotAI::IsOpeningOn); once the creature fights the team the fight
 * is TargetAssigner's. Every role: a team of one has no healer (RoleAssigner).
 * The hunt's opener (HuntPlan::opener) with a stealth (ClassKit::Stealth: rogue Stealth, cat
 * Prowl) casts it within STEALTH_FROM_YD of the creature: the walk in stealth is slower, the
 * rotation opens from it (Cheap Shot, Ambush, Pounce, Ravage).
 */

#ifndef MANGOS_COMMANDER_HUNT_BEHAVIOR_H
#define MANGOS_COMMANDER_HUNT_BEHAVIOR_H

#include "Commander/Behaviors/IBehavior.h"

namespace Commander
{
    class HuntBehavior : public IBehavior
    {
    public:
        // Points of starting the next fight now (Routine work, below anything assigned in a fight).
        static constexpr float HUNT_VALUE = 1.0f;
        // Stealth from this far (combat distance): about 6 s of the stealth walk
        // (PullControl::STEALTH_WALK_YD_PER_SEC) instead of 3 s running. Not in melee reach:
        // the opener goes now.
        static constexpr float STEALTH_FROM_YD = 20.0f;
        static constexpr float STEALTH_MIN_YD = 5.0f;

        char const* GetId() const override { return "hunt"; }
        bool AppliesTo(TeamBotAI const* bot) const override;
        void Propose(TeamBotAI const* bot, WorldModel const& world,
                     TeamPlan const& plan, std::vector<Proposal>& out) const override;
    };
}

#endif
