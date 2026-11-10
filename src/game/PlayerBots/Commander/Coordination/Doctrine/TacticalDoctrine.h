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
 * Groups, around the main tank (none without one):
 *   InCluster  within CLUSTER_YD of the tank (from the enemy's edge), or a melee enemy
 *              (Tags::Engage Melee) on its way there; except
 *   Special    a heavy enemy (Tags::Tier Heavy) not within CLUSTER_YD yet (a patrol walking in:
 *              held where it stands, it never gets into the pile);
 *   Outlier    the rest (casters standing back).
 * Phase:
 *   Gathering  the pull runs (Approach, Return, Settle) or the threat opening is on;
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
 * Stamps:
 *   NoMez      Area tactic and InCluster; Tier Fodder; Cleanup (every enemy); a player hits it.
 *   Committed  not NoMez, held by a bot's control that breaks on damage or takes one target at a
 *              time (ControlAbility::holding), and another enemy is still free: that bot renews
 *              it and does not move it.
 *   PlayerTarget  a player hits it (also NoMez: it would break anyway).
 *   The groups are stamps too, for the display.
 * Reads the pull, the threat opening and the retreat as their modules left them (this or the
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

        // A creature expected in a pull (PullControl, before the fight: no live facts yet).
        struct PullCreature
        {
            ObjectGuid guid;
            uint32 entry = 0;
            bool elite = false;
            uint32 maxHealth = 0;
        };
        // Before a pull: the expected creatures the doctrine will stamp NoMez once the fight
        // starts, by the same rules: the pile they make (melee ones that are not heavy) when it
        // is Area (area dealers and at least AREA_MIN of them), and fodder. `why` per guid.
        static std::vector<std::pair<ObjectGuid, char const*>> PullNoMez(WorldModel const& world,
            std::vector<PullCreature> const& expected);

        static char const* Name(FightPhase phase);
        static char const* Name(PileTactic tactic);
        static char const* Name(OutlierTactic tactic);
        // "in pile, no mez (area pile)"
        static std::string Describe(DoctrineEnemy const& d);
    };
}

#endif
