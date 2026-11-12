/*
 * PartyBot Commander layer 2 (brain), doctrine step: stamps from the enemies' roles and sources
 * (doctrine todo 9, user 2026-11-10; doc .kiro/docs/2026-11-09-doctrine/2026-11-10-roles-sources.md).
 * Called by TacticalDoctrine after its own stamps. Pure.
 *
 *   SelfDestruct  Tags::RolesOf TAG_SELF_DESTRUCT (Broken Cadaver). Nobody taunts, rescues or
 *                 controls it (it dies anyway). With a timed blast (Tags::BlastOf):
 *     KillNow     the party kills it before its earliest blast (health left / Value::DealtDps
 *                 + KILL_MARGIN_SEC < seconds to the earliest blast): the party
 *                 focus (TargetAssigner), damage dealers take it without the threat rule;
 *     StandClear  otherwise: every bot but its victim within its blast radius steps out until
 *                 the latest blast (Behaviors/Movement/BlastBehavior).
 *                 User 2026-11-10: kill it when the party can, step out when it cannot.
 *   Swarm         Tags::SourceOf Trap (Stratholme crate insects): no control of any kind
 *                 (CrowdControlAssigner), left out of the tank's threat risk (ThreatRisk::Counts); NoTaunt.
 *   NoTaunt       a swarm, a timed self-destruct, TAG_TAUNT_IMMUNE, TAG_TOTEM: no single target
 *                 taunt (RescueAssigner; winning it back with threat, area threat: still).
 *   KillOrder     TAG_HEALER, TAG_SUMMONER, TAG_TOTEM (user 2026-11-10: healers and summoners
 *                 first): the party focus is chosen among them while one is eligible
 *                 (TargetAssigner; after KillNow, before the outliers of KillFirst).
 *   HoldFire      EnemyFacts::reflectMs (a temporary damage shield or spell reflection): nobody
 *                 attacks, chases or casts damage at it while it lasts, every role alike
 *                 (TeamPlan::EngageBlock, Constraints); when it is the only enemy left, only
 *                 for one lasting at most REFLECT_ALONE_MAX_MS (else it is never killed).
 *   (mana drain and resurrects are KillOrder too.)
 *   Alarm         TAG_ALARM (summons as it aggroes): display; the summons are in the fight
 *                 estimate (FightLoad). Combat timer summons: TAG_SUMMONER (and FocusAssigner's
 *                 preventable summon); running for help: TAG_FLEES (FleePredictor).
 *   Only a timed blast is a SelfDestruct stamp: a walking bomb (blows when it reaches someone)
 *   is the intercept's (FocusAssigner).
 */

#ifndef MANGOS_COMMANDER_ROLE_STAMPS_H
#define MANGOS_COMMANDER_ROLE_STAMPS_H

namespace Commander
{
    struct WorldModel;
    struct DoctrinePlan;
    struct DoctrineEnemy;
    struct EnemyFacts;

    namespace RoleStamps
    {
        static constexpr float KILL_MARGIN_SEC = 0.5f;
        static constexpr uint32 REFLECT_ALONE_MAX_MS = 15 * 1000;

        // Source and roles on `x` (also for the display), then the stamps above.
        void Apply(WorldModel const& world, EnemyFacts const& e, DoctrineEnemy& x);
    }
}

#endif
