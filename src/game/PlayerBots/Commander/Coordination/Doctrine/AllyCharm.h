/*
 * PartyBot Commander layer 2 (brain), doctrine step: a party member under an enemy's mind
 * control (user 2026-11-10; doc .kiro/docs/2026-11-09-doctrine/2026-11-10-roles-sources.md 6).
 * Called by TacticalDoctrine after the stamps. Pure.
 *
 * For each charmed party member whose charmer is in the fight (PartyMemberFacts::charmer):
 *   - the charmer gets STAMP_FREE_ALLY: the party focus before anything else, damage dealers on
 *     it as a task (TargetAssigner, DamageTargets): killing it frees the ally.
 *   - hold: the charmer takes long to kill (a tough one; the user: "特别难打，肉的"): its health
 *     / Value::DealtDps and the charm's time left are both above QUICK_KILL_SEC. Then a bot
 *     also controls the ally until then (CrowdControlAssigner: Polymorph, a fear, a stun on the
 *     charmed player, whichever is worth most). A quick kill (an ordinary caster): kill only.
 *   The charm and a Polymorph left on the ally afterwards are magic: the dispel module removes
 *   them (DispelAssigner, charmed members: hostile dispels).
 * Writes DoctrinePlan::charmed.
 */

#ifndef MANGOS_COMMANDER_ALLY_CHARM_H
#define MANGOS_COMMANDER_ALLY_CHARM_H

namespace Commander
{
    struct WorldModel;
    struct DoctrinePlan;

    namespace AllyCharm
    {
        static constexpr float QUICK_KILL_SEC = 3.0f;

        void Apply(WorldModel const& world, DoctrinePlan& d);
    }
}

#endif
