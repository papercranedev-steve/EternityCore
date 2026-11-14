/*
 * PartyBot Commander layer 2 (brain), doctrine step: how this fight is fought (micro HTN, doc
 * .kiro/docs/2026-11-09-doctrine section 4, user 2026-11-09). Writes TeamPlan::doctrine:
 * phase, the pile tactic, and stamps on the enemies the duties read (CrowdControlAssigner).
 *
 * Why: the control module and the area phase each valued "control vs area spell" in points
 * for the moment they looked. Early in a pull the area spell was worth nothing yet, so the
 * priest shackled a ghoul in the pile, and from then on the pile got no Blizzard; two close
 * candidates also made it move its shackle (strat-20261108-140533). Now the doctrine decides
 * once and keeps it.
 *
 * Groups (Battlefield.h, since 2026-11-13): made from the pull's plan before the fight (phase
 *   Planned, no enemy yet: only DoctrinePlan::groups) and kept by guid into it; an enemy with
 *   no group gets one when it shows up (none without a main tank). Their stamps
 *   (Battlefield::StampOf):
 *   InCluster  the pile, a member of another group that came to the tank (one way), or a melee
 *              enemy on its way there; except
 *   Special    a heavy enemy of another group not at the tank yet (a patrol walking in);
 *   Outlier    the rest (casters standing back).
 *   Before 2026-11-13 the groups were made by distance every refresh (a heavy patrol one step
 *   inside CLUSTER_YD turned from Special to the pile's).
 * Phase:
 *   Planned    a pull is planned and nobody fights yet;
 *   Gathering  the pull runs (Approach, Return, Settle: the pack still coming, PullPlan::packComing);
 *   Cleanup    at most CLEANUP_FREE enemies not under a control;
 *   Clash      otherwise.
 * Tactic for the pile: Area when a damage dealer bot can cast an area spell (Tags::LedgerOf
 * areaDealers) and at least AREA_MIN enemies of the pile are not controlled; else Pick.
 *   Gathering: chosen again every refresh (the groups still change). Afterwards kept until an
 *   event: an add (an enemy not in `known`), the area dealers going to none or back, the Area
 *   pile thinning below AREA_KEEP, or the end of a retreat (none while retreating).
 * Enemies away from the tank (DoctrinePlan::outliers, FightPlace.h; todo 7, user 2026-11-10):
 *   Lure       the pull settles and its line of sight break was not searched yet: the enemies
 *              FightPlace::LureTargets picks (casters standing back; a corner fight's far ones)
 *              get STAMP_LURE, the pull's map step looks for the point; Active: its casters.
 *   KillFirst  otherwise, under the Area tactic with an Outlier left (also when no point was
 *              found): the party focus is among the outliers first (TargetAssigner, DamageTargets).
 *   None       otherwise.
 *   Where the pull fights (here, brought back, corner) is chosen before it: FightPlace::Choose.
 * The main tank's job (DoctrinePlan::tankJob, todo 12, user 2026-11-10): none while gathering;
 *   Pick: none (the tank follows the threat risk, TankTargets; Focus until 2026-11-13);
 *   Area: HoldPile (its targets by the threat risk too; an enemy on the area caster is an Urgent
 *   rescue, two or more of them make a group threat Urgent: Challenging Shout).
 * Stamps:
 *   NoMez      Area tactic and InCluster; Tier Fodder; Cleanup (every enemy); a player hits it.
 *   Committed  not NoMez, held by a bot's control that breaks on damage or takes one target at a
 *              time (ControlAbility::holding), and another enemy is still free: that bot renews
 *              it and does not move it.
 *   PlayerTarget  a player hits it (also NoMez: it would break anyway).
 *   NoHold     Tier Fodder or a trap swarm, not a self-destruct: damage dealers do not wait
 *              for the tank's threat on it.
 *   The groups are stamps too, for the display.
 * Reads the pull and the retreat as their modules left them (this or the
 * last refresh: one refresh late does not matter here).
 * Pure (facts and plan).
 */

#ifndef MANGOS_COMMANDER_TACTICAL_DOCTRINE_H
#define MANGOS_COMMANDER_TACTICAL_DOCTRINE_H

#include "Commander/Coordination/IAssigner.h"
#include "ObjectGuid.h"
#include <string>
#include <utility>
#include <vector>

namespace Commander
{
    struct DoctrineEnemy;
    enum class FightPhase : uint8;
    enum class PileTactic : uint8;
    enum class OutlierTactic : uint8;
    enum class TankJob : uint8;

    class TacticalDoctrine : public IAssigner
    {
    public:
        static constexpr float CLUSTER_YD = 10.0f;
        static constexpr uint32 AREA_MIN = 3;
        static constexpr uint32 AREA_KEEP = 2;
        static constexpr uint32 CLEANUP_FREE = 1;

        char const* GetId() const override { return "doctrine"; }
        AssignStage GetStage() const override { return AssignStage::Situation; }
        void Assign(WorldModel const& world, TeamPlan& plan) const override;
        void Clear(TeamPlan& plan) const override;

        static char const* Name(FightPhase phase);
        static char const* Name(PileTactic tactic);
        static char const* Name(OutlierTactic tactic);
        static char const* Name(TankJob job);
        // "melee, elite, in pile, no mez (area pile), from death spawn, roles: healer, kill first"
        // (also the combat recording's plan line, u:plan().doctrine)
        static std::string Describe(DoctrineEnemy const& d);
    };
}

#endif
