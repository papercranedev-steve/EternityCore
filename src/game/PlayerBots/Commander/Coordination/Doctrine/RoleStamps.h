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
 *   Swarm         Tags::SourceOf Trap (Stratholme crate insects): no single target taunt
 *                 (RescueAssigner; area threat still), no control of any kind
 *                 (CrowdControlAssigner), left out of the threat opening (ThreatOpening).
 *   Alarm         TAG_ALARM (aggro summons, a summon combat timer, runs for help): display; the
 *                 summons are in the fight estimate (FightLoad), the timers in FocusAssigner,
 *                 the run in FleePredictor.
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

        // Source and roles on `x` (also for the display), then the stamps above.
        void Apply(WorldModel const& world, EnemyFacts const& e, DoctrineEnemy& x);
    }
}

#endif
