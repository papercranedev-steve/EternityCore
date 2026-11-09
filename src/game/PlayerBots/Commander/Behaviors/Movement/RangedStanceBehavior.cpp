#include "Commander/Behaviors/Movement/RangedStanceBehavior.h"
#include "Commander/Perception/Spatial/SafeRoute.h"
#include "Commander/Perception/Spatial/HazardZones.h"
#include "Commander/Perception/Values/ValueModel.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Coordination/Situation/Patrol/PatrolAssigner.h"     // InPath: pure, facts only
#include "PartyBotAI.h"
#include "Map.h"
#include <algorithm>
#include <cmath>

namespace Commander
{
    namespace
    {
        using RS = RangedStanceBehavior;
        using SafeRoute::Distance2d;

        // Distances from the victim's edge, the old chase distance first.
        float const RADII[] = { 25.0f, 20.0f, 15.0f };
        // Before the fight (planned fight spot): one more ring at the edge of cast range, for a
        // walk-in fought where the pack stands (the anchor is the idle pack, inner rings lie in
        // its aggro range).
        float const PREPARE_RADII[] = { 28.0f, 25.0f, 20.0f, 15.0f };

        Position3 PositionOf(WorldObject const* obj)
        {
            Position3 p;
            p.x = obj->GetPositionX();
            p.y = obj->GetPositionY();
            p.z = obj->GetPositionZ();
            return p;
        }

        bool Los(Map* map, Position3 const& a, Position3 const& b)
        {
            return map->isInLineOfSight(a.x, a.y, a.z + RS::LOS_EYE_HEIGHT, b.x, b.y, b.z + RS::LOS_EYE_HEIGHT);
        }

        bool InPatrolPath(WorldModel const& world, Position3 const& p)
        {
            float const remaining = Value::FightRemainingSec(world);
            for (PatrolFacts const& patrol : world.patrols)
                if (patrol.aggressive && !patrol.noAggro && !patrol.tappedByOther &&
                    PatrolAssigner::InPath(patrol, p, remaining))
                    return true;
            return false;
        }

        struct Spot
        {
            Position3 pos;
            float walk = 0.0f;
        };

        // Ring points passing the cheap checks (no map queries), nearest walk first.
        std::vector<Spot> Candidates(WorldModel const& world, SafeRoute::IdleCreatures const& idle,
                                     std::vector<HazardZone> const& zones, Position3 const& here,
                                     Position3 const& victim, float victimReach, bool prepare)
        {
            std::vector<Spot> spots;
            float const start = std::atan2(here.y - victim.y, here.x - victim.x);
            float const* radii = prepare ? PREPARE_RADII : RADII;
            size_t const radiusCount = prepare ? sizeof(PREPARE_RADII) / sizeof(float) : sizeof(RADII) / sizeof(float);
            for (size_t i = 0; i < radiusCount; ++i)
            {
                float const r = radii[i];
                for (uint32 k = 0; k < RS::RING_POINTS; ++k)
                {
                    // 0, +1, -1, +2, -2 ... steps away from the bot's side.
                    int32 const step = int32((k + 1) / 2) * ((k % 2) ? 1 : -1);
                    float const angle = start + 2.0f * float(M_PI) * step / RS::RING_POINTS;
                    Spot s;
                    s.pos.x = victim.x + (r + victimReach) * std::cos(angle);
                    s.pos.y = victim.y + (r + victimReach) * std::sin(angle);
                    s.pos.z = victim.z;
                    if (HazardZones::IsInside(zones, HAZARD_RANGED, s.pos) || InPatrolPath(world, s.pos) || idle.Near(s.pos))
                        continue;
                    s.walk = Distance2d(here, s.pos);
                    spots.push_back(s);
                }
            }
            std::stable_sort(spots.begin(), spots.end(), [](Spot const& a, Spot const& b) { return a.walk < b.walk; });
            return spots;
        }

        // Map checks: ground near the victim's height, out of idle reach there, line of sight
        // to the victim, safe path. `pathIdle`: `idle`, or its LeavingFrom(here) copy when the
        // bot stands in reach now.
        bool GeometryOk(Player* me, SafeRoute::IdleCreatures const& idle, SafeRoute::IdleCreatures const& pathIdle,
                        Position3 const& victim, Spot& s)
        {
            Map* map = me->GetMap();
            float const ground = map->GetHeight(s.pos.x, s.pos.y, victim.z + 3.0f);
            if (ground <= INVALID_HEIGHT || std::fabs(ground - victim.z) > RS::MAX_STEP_Z)
                return false;
            s.pos.z = ground;
            if (idle.Near(s.pos) || !Los(map, s.pos, victim))
                return false;
            return SafeRoute::PathIsSafe(me, pathIdle, s.pos, RS::MAX_PATH_DETOUR);
        }
    }

    bool RangedStanceBehavior::AppliesTo(PartyBotAI const* bot) const
    {
        return bot->GetRole() == ROLE_RANGE_DPS;
    }

    bool RangedStanceBehavior::TakesOver(Takeover what, PartyBotAI const* bot, WorldModel const& world,
                                         TeamPlan const& /*plan*/) const
    {
        return what == Takeover::Stance && world.InFight(bot->me->IsInCombat()) && !bot->IsInDuel() &&
               !bot->me->GetTransport();
    }

    void RangedStanceBehavior::Propose(PartyBotAI const* bot, WorldModel const& world,
                                       TeamPlan const& plan, std::vector<Proposal>& out) const
    {
        // The retreat owns movement (Override tier).
        if (plan.retreat.active)
            return;
        Player* me = bot->me;
        if (me->HasUnitState(UNIT_STATE_ROOT) || !bot->UsesCasterChase())
            return;

        // Before the fight: out of combat while the pull runs, take a place in cast range of
        // where the brain planned the tank to fight (PullPlan::PlannedFightSpot), then wait.
        bool const inCombat = me->IsInCombat();
        Position3 planned;
        bool const prepare = !inCombat && plan.pull.PlannedFightSpot(planned);
        if (!prepare)
        {
            // In combat the pull owns movement while the tank goes out and brings the pack back.
            if (plan.pull.phase == PullPhase::Approach || plan.pull.phase == PullPhase::Return)
                return;
            if (!world.InFight(inCombat))
                return;
            // Still out of combat and told to wait by the pull (opening hold).
            if (!inCombat && plan.pull.HoldsWaiting(false))
                return;
        }

        // A MoveTo (this module's or another's) is under way: keep walking. Without a proposal
        // the rotation's first cast stops the walk (DoCastSpell -> StopMoving).
        if (bot->IsMovementHeld() && me->IsMoving())
        {
            Proposal hold;
            hold.tier = ProposalTier::Assigned;
            hold.score = 0.0f;
            hold.source = "rangedstance";
            hold.reason = "walking to a spot, keep walking";
            hold.action.type = BotActionType::Hold;
            out.push_back(hold);
            return;
        }
        if (me->IsNonMeleeSpellCasted(false, false, true))
            return;

        PartyMemberFacts const* self = world.FindMember(me->GetObjectGuid());
        if (!self || !self->CanAct())
            return;
        Position3 const here = PositionOf(me);
        Position3 at;               // what the bot must reach: its victim, or the planned fight spot
        float atReach = 0.0f;
        bool inReach = false;
        if (prepare)
        {
            at = planned;
            inReach = Distance2d(here, at) <= CAST_RANGE && Los(me->GetMap(), here, at);
        }
        else
        {
            // No victim yet: the target the brain gave it. TargetBehavior only attacks it in
            // range and sight, so without this the bot stood where it was (it no longer follows
            // the leader during the party's fight).
            Unit* victim = me->GetVictim();
            if (!victim)
                if (TargetDuty const* duty = plan.FindTargetFor(me->GetObjectGuid()))
                    victim = me->GetMap()->GetUnit(duty->enemy);
            if (!victim || !victim->IsAlive())
                return;
            // It comes for the bot: running from melee is the escape module's job.
            if (victim->GetVictim() == me)
                return;
            at = PositionOf(victim);
            atReach = victim->GetCombatReach();
            inReach = me->GetCombatDistance(victim) <= CAST_RANGE && Los(me->GetMap(), here, at);
        }

        SafeRoute::IdleCreatures const idle(me);
        bool const unsafe = idle.Near(here);
        if (!unsafe && inReach)
            return;

        std::vector<HazardZone> const& zones = HazardZones::Get(world);
        std::vector<Spot> spots = Candidates(world, idle, zones, here, at, atReach, prepare);
        // The walk out of an idle creature's reach starts inside it.
        SafeRoute::IdleCreatures const pathIdle = unsafe ? idle.LeavingFrom(here) : idle;
        uint32 checks = 0;
        for (Spot& s : spots)
        {
            if (checks >= MAX_GEOMETRY_CHECKS)
                break;
            ++checks;
            if (!GeometryOk(me, idle, pathIdle, at, s))
                continue;

            float const speed = std::max(1.0f, me->GetSpeed(MOVE_RUN));
            float const walkSec = Distance2d(here, s.pos) * PATH_FACTOR / speed;
            float const horizon = std::max(1.0f, std::min(HORIZON_SEC, Value::FightRemainingSec(world)));
            Proposal p;
            p.source = "rangedstance";
            p.action.type = BotActionType::MoveTo;
            p.action.x = s.pos.x;
            p.action.y = s.pos.y;
            p.action.z = s.pos.z;
            p.action.holdMs = uint32(walkSec * 1000.0f) + HOLD_EXTRA_MS;
            if (unsafe)
            {
                // A constraint: never stand where an idle pack would come.
                p.tier = ProposalTier::Urgent;
                p.reason = "an idle creature would attack here";
                p.score = -walkSec;
            }
            else if (prepare)
            {
                // Nothing to do before the fight: the walk costs nothing now and saves walking
                // it once the fight is on (its part of the planned fight, FightLoad estimate).
                float const incoming = std::max(world.rates.incomingDps, plan.pull.expectedIncomingDps);
                p.tier = ProposalTier::Assigned;
                p.reason = "take a place for the planned fight";
                p.SetValue(Value::ContributionPerSecAt(world, *self, incoming) * walkSec, 0.0f);
            }
            else
            {
                p.tier = ProposalTier::Assigned;
                p.reason = "victim out of range or sight";
                p.SetValue(Value::LostAction(world, *self, horizon), Value::LostAction(world, *self, walkSec));
            }
            out.push_back(p);
            return;
        }
    }
}
