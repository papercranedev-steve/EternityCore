#include "Commander/Behaviors/Movement/KiteBehavior.h"
#include "Commander/Behaviors/Movement/RangedSpot.h"
#include "Commander/BotDirectives.h"
#include "Commander/Classes/ClassKit.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Coordination/Situation/Patrol/RouteGuard.h"
#include "Commander/Perception/Spatial/PositionField.h"
#include "Commander/Perception/Spatial/SafeRoute.h"
#include "Commander/Perception/Values/Exposure.h"
#include "Commander/Perception/Values/SightMargin.h"
#include "Commander/Perception/Values/ValueModel.h"
#include "Commander/Perception/WorldModel.h"
#include "TeamBotAI.h"
#include "Map.h"
#include "Pet.h"
#include "SpellEntry.h"
#include "Timer.h"
#include <algorithm>
#include <cstring>

namespace Commander
{
    namespace
    {
        using KB = KiteBehavior;
        using SafeRoute::Distance2d;

        struct Chaser
        {
            EnemyFacts const* facts = nullptr;
            Unit* unit = nullptr;
            bool held = false;      // rooted or stunned
        };

        Proposal Make(char const* reason)
        {
            Proposal p;
            p.tier = ProposalTier::Assigned;
            p.source = "kite";
            p.reason = reason;
            return p;
        }

        // Speed factor of `spell`'s slow (0.5 for -50%), 1 = no slow the chaser takes.
        float SlowFactor(SpellEntry const* spell, Unit const* chaser)
        {
            float factor = 1.0f;
            for (uint32 i = 0; i < MAX_SPELL_EFFECTS; ++i)
            {
                if (spell->Effect[i] != SPELL_EFFECT_APPLY_AURA ||
                    spell->EffectApplyAuraName[i] != SPELL_AURA_MOD_DECREASE_SPEED)
                    continue;
                if (chaser->IsImmuneToSpellEffect(spell, SpellEffectIndex(i), false))
                    continue;
                int32 const pct = spell->CalculateSimpleValue(SpellEffectIndex(i));
                factor = std::min(factor, std::max(0.0f, 1.0f + float(pct) / 100.0f));
            }
            return factor;
        }

        // Centre gap to the chaser at which the bot arrives ARRIVE_MARGIN beyond its dead zone,
        // walking straight away from `nowGap` while the chaser follows at `ratio` x its speed:
        // gap - ratio x (gap - nowGap) >= arrive.
        float Gap(float arrive, float nowGap, float ratio)
        {
            return std::max(arrive, (arrive - ratio * nowGap) / std::max(0.05f, 1.0f - ratio));
        }

        // Header 1: walk plus the chaser's way back from `arriveGap` to melee, capped.
        float FreeSec(WorldModel const& world, Chaser const& c, float walkSec, float arriveGap,
                      float meleeGap, float chaserSpeed)
        {
            float back = chaserSpeed > 0.1f ? std::max(0.0f, arriveGap - meleeGap) / chaserSpeed : KB::FREE_CAP_SEC;
            float const sec = std::min(KB::FREE_CAP_SEC, walkSec + back);
            return std::min(sec, std::max(1.0f, Value::EnemyTimeToDie(world, *c.facts)));
        }

        float Saved(WorldModel const& world, Chaser const& c, PartyMemberFacts const& self, float seconds)
        {
            return Value::HitLoss(world, *c.facts, self, seconds) + Value::HarassLoss(world, *c.facts, self, seconds);
        }
    }

    bool KiteBehavior::AppliesTo(TeamBotAI const* bot) const
    {
        return bot->Kit().MinAttackRange() > 0.0f;
    }

    void KiteBehavior::Propose(TeamBotAI const* bot, WorldModel const& world,
                               TeamPlan const& plan, std::vector<Proposal>& out) const
    {
        // The retreat owns movement (Override tier); the pull owns it while the tank goes out
        // and brings the pack back.
        if (plan.retreat.active)
            return;
        if (plan.pull.phase == PullPhase::Approach || plan.pull.phase == PullPhase::Return)
            return;
        Player* me = bot->me;
        if (!me->IsInCombat() || me->HasUnitState(UNIT_STATE_ROOT))
            return;
        PartyMemberFacts const* self = world.FindMember(me->GetObjectGuid());
        if (!self || !self->CanAct())
            return;

        // Our own run is under way: keep walking (a cast-time spell of the rotation would stop it).
        if (bot->IsMovementHeld() && me->IsMoving())
        {
            char const* owner = bot->Directives().GetMovementOwner(WorldTimer::getMSTime());
            if (owner && strcmp(owner, "kite") == 0)
            {
                Proposal hold = Make("out of the dead zone, keep running");
                hold.score = 0.0f;
                hold.action.type = BotActionType::KeepWalking;
                out.push_back(hold);
            }
            return;
        }

        float const deadZone = bot->Kit().MinAttackRange();
        Position3 here;
        here.x = me->GetPositionX();
        here.y = me->GetPositionY();
        here.z = me->GetPositionZ();
        float const myRadius = me->GetObjectBoundingRadius();

        Map* map = me->GetMap();
        Chaser chaser;
        float nearest = 0.0f;
        for (EnemyFacts const& e : world.enemies)
        {
            if (!e.acting || !e.meleeEnabled || e.victim != self->guid)
                continue;
            float const edge = Distance2d(here, e.pos) - e.boundingRadius - myRadius;
            if (edge >= deadZone || (chaser.facts && edge >= nearest))
                continue;
            Unit* unit = map->GetUnit(e.guid);
            if (!unit || !unit->IsAlive())
                continue;
            bool const stunned = unit->HasUnitState(UNIT_STATE_STUNNED);
            if (e.crowdControlled && !stunned)
                continue;   // polymorphed, feared: not coming for the bot now
            chaser.facts = &e;
            chaser.unit = unit;
            chaser.held = stunned || e.rooted;
            nearest = edge;
        }
        if (!chaser.facts)
            return;

        EnemyFacts const& e = *chaser.facts;
        float const speed = std::max(1.0f, me->GetSpeed(MOVE_RUN));
        float const nowGap = Distance2d(here, e.pos);
        float const arrive = deadZone + ARRIVE_MARGIN + e.boundingRadius + myRadius;
        float const meleeGap = MELEE_GAP + e.boundingRadius + myRadius;

        // 1. Held or slow enough: run out of the dead zone, to a spot the ranged stance would
        // take too (RangedSpot.h: idle packs, patrols, hazards, exposure, sight of the chaser).
        float const ratio = chaser.held ? 0.0f : e.speed / speed;
        if (ratio <= MAX_SPEED_RATIO)
        {
            if (!world.field || !world.field->sight)
                return;
            float const chaserSpeed = chaser.held ? 0.0f : e.speed;
            SafeRoute::IdleCreatures const idle(me);
            std::vector<EnemyFacts const*> const turnable = Exposure::Turnable(world, self->guid);
            std::vector<Position3> const points = SightMargin::Points(world, e.pos, e.guid, self->guid);
            RangedSpot::Query q;
            q.at = e.pos;
            q.minYd = Gap(arrive, nowGap, ratio);
            q.maxYd = q.minYd + BAND_YD;
            q.staySec = RouteGuard::StaySec(world, plan);
            q.turnable = &turnable;
            q.points = &points;
            q.unsafe = idle.ArrivingAt(e.pos).Near(here);
            // The bot gets there ARRIVE_MARGIN beyond its dead zone while the chaser follows,
            // and on its own side of the chaser (not past it).
            q.keep = [&](RangedSpot::Spot const& s)
            {
                float const gap = Distance2d(s.pos, e.pos);
                return gap - chaserSpeed * s.walkSec >= arrive && Distance2d(here, s.pos) < gap;
            };
            uint32 budget = MAX_SIGHT_CHECKS;
            RangedSpot::Result found;
            if (!RangedSpot::Find(me, world, *self, idle, q, budget, found))
                return;
            RangedSpot::Spot const& s = found.spot;
            float const arriveGap = Distance2d(s.pos, e.pos) - chaserSpeed * s.walkSec;
            Proposal p = Make(chaser.held ? "melee enemy held: out of the dead zone" : "melee enemy slowed: out of the dead zone");
            p.action.type = BotActionType::MoveTo;
            p.action.x = s.pos.x;
            p.action.y = s.pos.y;
            p.action.z = s.pos.z;
            p.action.holdMs = uint32(s.walkSec * 1000.0f) + HOLD_EXTRA_MS;
            p.SetValue(Saved(world, chaser, *self, FreeSec(world, chaser, s.walkSec, arriveGap, meleeGap, chaserSpeed)),
                       Value::LostAction(world, *self, s.walkSec) + std::max(0.0f, s.exposure + s.sight));
            out.push_back(p);
            return;
        }

        // The run a slow or a stun would allow (header 2, 3), estimated without a spot.
        auto const runValue = [&](float slowedSpeed, float& walkSec) -> float
        {
            float const r = slowedSpeed / speed;
            float const gap = Gap(arrive, nowGap, r);
            walkSec = std::max(0.0f, gap - nowGap) * PATH_FACTOR / speed;
            float const arriveGap = gap - slowedSpeed * walkSec;
            return Saved(world, chaser, *self, FreeSec(world, chaser, walkSec, arriveGap, meleeGap, slowedSpeed));
        };

        // 2. A slow that takes.
        for (SpellEntry const* spell : bot->Kit().KiteSlows())
        {
            if (!spell || !bot->CanTryToCastSpell(chaser.unit, spell))
                continue;
            float const slowed = e.speed * SlowFactor(spell, chaser.unit);
            if (slowed / speed > MAX_SPEED_RATIO)
                continue;
            float walkSec = 0.0f;
            float const benefit = runValue(slowed, walkSec);
            Proposal p = Make("melee enemy in the dead zone: slow it, then step out");
            p.action.type = BotActionType::Cast;
            p.action.target = e.guid;
            p.action.spell = spell;
            p.SetValue(benefit, Value::LostAction(world, *self, Value::GCD_SEC + walkSec));
            out.push_back(p);
            return;
        }

        // 3. The pet stuns it with its next hit. Not a trap swarm or a self-destruct: the doctrine
        // allows no control on them (RoleStamps.h; strat-20261124-231028: Intimidation spent on
        // a crate insect).
        if (plan.doctrine.Has(e.guid, STAMP_SWARM | STAMP_SELF_DESTRUCT))
            return;
        SpellEntry const* stun = bot->Kit().PetStun();
        Pet* pet = me->GetPet();
        if (!stun || !pet || !pet->IsAlive() || pet->GetVictim() != chaser.unit ||
            chaser.unit->IsImmuneToMechanic(MECHANIC_STUN) || !bot->CanTryToCastSpell(pet, stun))
            return;
        float walkSec = 0.0f;
        float const benefit = runValue(0.0f, walkSec);
        Proposal p = Make("melee enemy in the dead zone: the pet stuns it, then step out");
        p.action.type = BotActionType::Cast;
        p.action.target = pet->GetObjectGuid();
        p.action.spell = stun;
        p.SetValue(benefit, Value::LostAction(world, *self, Value::GCD_SEC + walkSec));
        out.push_back(p);
    }
}
