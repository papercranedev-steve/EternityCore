/*
 * PartyBot Commander layer 2 (brain): who puts which control spell on which enemy, in combat
 * (crowd control doc .kiro/docs/2026-11-07/2026-11-07-crowd-control.md, todo 10b). Duties
 * module; raid marks are not read (user, 2026-11-07: the brain picks by itself).
 *
 * Offer, every refresh while the party fights (FightLoad::FromWorld built once):
 *   single target spell, per enemy of ours that acts and the spell affects (ControlValue::
 *   Affects), within its range + REACH_SLACK_YD of the bot, not under a control lasting
 *   longer than the spell's cast + RECAST_LEAD_SEC (re-control comes in time, not earlier):
 *     the doctrine's stamps first (Coordination/Doctrine/TacticalDoctrine.h, doc
 *     .kiro/docs/2026-11-09-doctrine 5): an enemy stamped NoMez gets no control that breaks
 *     on damage and no fear (a stun is fine); a oneTarget spell holding a Committed enemy is
 *     offered on that enemy only (renewed, never moved). Ruled out ones are logged as rejected.
 *     worth = ControlValue::Of (whether the pile is left to area spells is the doctrine's,
 *     not a cost here since 2026-11-09). Candidate when worth.Net() > 0, rank = net (+ STICKY_POINTS for the bot that controls
 *     it now or was given it last refresh). One need per enemy, points = best net, now = true.
 *   area spell (Psychic Scream, Intimidating Shout, Howl of Terror): the enemies within its
 *     radius of the bot, none of them NoMez; one need per bot and spell (target = the first
 *     enemy, detail AREA).
 *     Fears do not break on damage: no aoe cost; their add risk and scatter are in the worth.
 *   Sap (target out of combat) is never offered here: before a pull only (PullControl.h,
 *     CrowdControlBehavior casts it there).
 *   opening control of the pull (PullPlan::control, PullControl.h) cast in the fight: once
 *     its target is in the fight and under no control, one need with the planned controller
 *     only and the planned points; that enemy gets no other offer meanwhile. Not when the
 *     doctrine stamped it NoMez (known limit: the controller walked to its spot for nothing).
 * Apply: plan.controls from the results; a single target duty on an enemy an assigned area
 * spell also takes is dropped. plan.protectedEnemies = targets of duties that break on damage
 * (TeamPlan::KeepsUnbroken: area spells leave them alone from this refresh on).
 *
 * One primary duty per bot of any kind (DutyAllocator): a mage interrupting is not also given
 * a sheep; a more valuable sheep takes the mage and the interrupt goes to its backup.
 */

#ifndef MANGOS_COMMANDER_CROWD_CONTROL_ASSIGNER_H
#define MANGOS_COMMANDER_CROWD_CONTROL_ASSIGNER_H

#include "Commander/Coordination/IAssigner.h"

namespace Commander
{
    class CrowdControlAssigner : public IAssigner
    {
    public:
        static constexpr float REACH_SLACK_YD = 10.0f;     // a few steps into range are in ControlValue's reach cost
        static constexpr float RECAST_LEAD_SEC = 1.0f;
        static constexpr float STICKY_POINTS = 0.5f;
        // DutyNeed::detail bits.
        static constexpr uint8 DETAIL_AREA = 1;
        static constexpr uint8 DETAIL_BREAKS = 2;

        // Same id as CrowdControlBehavior: `.partybot ai module cc off` switches both.
        char const* GetId() const override { return "cc"; }
        AssignStage GetStage() const override { return AssignStage::Duties; }
        void Offer(WorldModel const& world, TeamPlan const& plan, std::vector<DutyNeed>& out) const override;
        void Apply(WorldModel const& world, std::vector<DutyResult> const& results, TeamPlan& plan) const override;
        void Clear(TeamPlan& plan) const override;

        // Creature type mask of a player (CREATURE_TYPE_HUMANOID 7): controls that take a
        // mind controlled party member.
        static constexpr uint32 HUMANOID_TYPE_MASK = 1u << (7 - 1);

    private:
        // Mind controlled party members the doctrine holds (Coordination/Doctrine/AllyCharm.h):
        // one need per ally, any single target control that works on a player.
        void OfferCharmedAllies(WorldModel const& world, TeamPlan const& plan, std::vector<DutyNeed>& out) const;
    };
}

#endif
