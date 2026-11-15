/*
 * PartyBot Commander layer 2 (brain), doctrine step: "if the estimate says we can't win,
 * control" (battlefield groups doc .kiro/docs/2026-11-09-doctrine/2026-11-13-battlefield-groups.md
 * 5, user 2026-11-13). Runs in TacticalDoctrine after the stamps; writes DoctrinePlan::holds and
 * stamps their enemies STAMP_MUST_CONTROL.
 *
 * Kept holds (from the last refresh): dropped when the enemy is gone (a planned patrol hold:
 * also while it is still walking, WorldModel::patrols), the controller cannot act or no longer
 * has the spell, or the enemy is stamped NoMez and the hold is not a Hold kind control (it walked
 * into the area pile, a player hits it; cleanup only once no other enemy is free, TacticalDoctrine
 * stamps). A resisted cast is not a drop: the controller casts again
 * (it is its task). A drop is a reselect event.
 *
 * The check, at a reselect event (first choice, an enemy not known before, a dropped hold):
 *   the fight now (FightLoad::FromWorld) with every kept hold held; bearable (FightLoad::Bearable):
 *   done. Else, one at a time, the hold that raises FightLoad::Score the most, until bearable:
 *   enemies   acting, free, no hold yet; not a swarm, a self-destruct, fodder or a player's target;
 *   controls  ControlValue::OptionsFor: ready, not a fear (it scatters the fight), not the tank's,
 *             at most one hold per bot, not a one target spell that holds another enemy now (it
 *             would free that one); on an enemy stamped NoMez (the area pile) only a Hold kind
 *             (stun, banish: damage does not end it);
 *   who       bots without area spells first; the area dealers only when none of those helps
 *             (user 2026-11-13);
 *   held      a Hold kind: its duration; a Mez (renewed by its controller while the doctrine
 *             holds it): until the rest of the fight is over; then it joins (sameFight).
 *   Nothing raises the score: "not bearable, no control left" (said once), the fight goes on.
 *   points    FightLoad::Loss as is - held: what the duty is worth (CrowdControlAssigner).
 * Pure (facts and plan).
 */

#ifndef MANGOS_COMMANDER_TACTIC_CHECK_H
#define MANGOS_COMMANDER_TACTIC_CHECK_H

#include "Commander/Coordination/TeamPlan.h"

namespace Commander
{
namespace TacticCheck
{
    // `recheck`: a reselect event this refresh (header).
    void Apply(WorldModel const& world, TeamPlan& plan, DoctrinePlan const& prev, DoctrinePlan& d, bool recheck);
}
}

#endif
