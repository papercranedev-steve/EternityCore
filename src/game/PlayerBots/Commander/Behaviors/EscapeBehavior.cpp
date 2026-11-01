#include "Commander/Behaviors/EscapeBehavior.h"
#include "Commander/Behaviors/FleePoint.h"
#include "Commander/Behaviors/SafeRoute.h"
#include "Commander/Perception/Tanks.h"
#include "Commander/Perception/ValueModel.h"
#include "Commander/Coordination/TeamPlan.h"
#include "PartyBotAI.h"
#include "DBCStores.h"
#include "Map.h"
#include "SpellEntry.h"
#include <algorithm>
#include <cmath>

namespace Commander
{
    namespace
    {
        using EB = EscapeBehavior;
        using SafeRoute::Distance2d;

        struct Chaser
        {
            EnemyFacts const* facts;
            Unit* unit;
            bool rooted;
        };

        // A spell that roots the enemies around the bot so it can walk away. Mage only for now;
        // other classes add theirs here.
        SpellEntry const* RootSpell(PartyBotAI const* bot)
        {
            if (bot->me->GetClass() == CLASS_MAGE)
                return bot->m_spells.mage.pFrostNova;
            return nullptr;
        }

        float SpellRadius(SpellEntry const* spell)
        {
            float radius = 0.0f;
            for (uint32 i = 0; i < MAX_SPELL_EFFECTS; ++i)
                if (SpellRadiusEntry const* r = sSpellRadiusStore.LookupEntry(spell->EffectRadiusIndex[i]))
                    radius = std::max(radius, r->Radius);
            return radius;
        }

        Proposal MakeProposal(ProposalTier tier, char const* reason)
        {
            Proposal p;
            p.tier = tier;
            p.source = "escape";
            p.reason = reason;
            return p;
        }

        void SetMove(Proposal& p, Position3 const& to, float walkSec)
        {
            p.action.type = BotActionType::MoveTo;
            p.action.x = to.x;
            p.action.y = to.y;
            p.action.z = to.z;
            p.action.holdMs = uint32(walkSec * 1000.0f) + EB::HOLD_EXTRA_MS;
        }

        // Hits and harassment saved while the chasers in `chasers` cannot reach the bot for `seconds`.
        float SavedWhileAway(WorldModel const& world, std::vector<Chaser> const& chasers,
                             PartyMemberFacts const& self, float seconds)
        {
            float saved = 0.0f;
            for (Chaser const& c : chasers)
                saved += Value::HitLoss(world, *c.facts, self, seconds) + Value::HarassLoss(world, *c.facts, self, seconds);
            return saved;
        }
    }

    bool EscapeBehavior::AppliesTo(PartyBotAI const* bot) const
    {
        return bot->GetRole() == ROLE_HEALER || bot->GetRole() == ROLE_RANGE_DPS;
    }

    bool EscapeBehavior::ControlsEscape(PartyBotAI const* bot) const
    {
        return AppliesTo(bot) && !bot->IsInDuel();
    }

    void EscapeBehavior::Propose(PartyBotAI const* bot, WorldModel const& world,
                                 TeamPlan const& plan, std::vector<Proposal>& out) const
    {
        // The retreat owns movement (Override tier); the pull owns it while the tank goes out
        // and brings the pack back.
        if (plan.retreat.active)
            return;
        if (plan.pull.phase == PullPhase::Approach || plan.pull.phase == PullPhase::Return)
            return;

        Player* me = bot->me;
        if (!me->IsInCombat() || me->HasUnitState(UNIT_STATE_ROOT) || bot->IsInDuel())
            return;
        PartyMemberFacts const* self = world.FindMember(me->GetObjectGuid());
        if (!self || !self->CanAct())
            return;

        Map* map = me->GetMap();
        Position3 here;
        here.x = me->GetPositionX();
        here.y = me->GetPositionY();
        here.z = me->GetPositionZ();

        std::vector<Chaser> chasers;
        for (EnemyFacts const& e : world.enemies)
        {
            if (!e.acting || e.crowdControlled || !e.meleeEnabled || e.victim != self->guid)
                continue;
            if (Distance2d(here, e.pos) - e.boundingRadius > TRIGGER_GAP)
                continue;
            Unit* unit = map->GetUnit(e.guid);
            if (!unit || !unit->IsAlive())
                continue;
            chasers.push_back({ &e, unit, unit->HasUnitState(UNIT_STATE_ROOT) });
        }
        if (chasers.empty())
            return;

        // A MoveTo (this module's or another's) is under way: keep walking (a cast-time spell
        // of the rotation would stop it).
        if (bot->IsMovementHeld() && me->IsMoving())
        {
            Proposal hold = MakeProposal(ProposalTier::Assigned, "running away, keep running");
            hold.score = 0.0f;
            hold.action.type = BotActionType::Hold;
            out.push_back(hold);
            return;
        }

        float const incoming = Value::IncomingOn(world, self->guid);
        bool const dangerous = chasers.size() >= DANGER_CHASERS ||
                               (incoming > 0.0f && float(self->health) / incoming < DANGER_SEC);
        bool const allRooted = std::all_of(chasers.begin(), chasers.end(), [](Chaser const& c) { return c.rooted; });

        // 1. Root them first when the hits are dangerous.
        if (dangerous && !allRooted)
        {
            if (SpellEntry const* root = RootSpell(bot))
            {
                if (bot->CanTryToCastSpell(me, root))
                {
                    float const radius = SpellRadius(root);
                    std::vector<Chaser> inRadius;
                    for (Chaser const& c : chasers)
                        if (!c.rooted && me->IsWithinDistInMap(c.unit, radius))
                            inRadius.push_back(c);
                    if (!inRadius.empty())
                    {
                        Proposal p = MakeProposal(ProposalTier::Urgent, "dangerous hits: root them");
                        p.action.type = BotActionType::Cast;
                        p.action.spell = root;
                        p.SetValue(SavedWhileAway(world, inRadius, *self, ROOT_HOLD_SEC),
                                   Value::LostAction(world, *self, Value::GCD_SEC));
                        out.push_back(p);
                        return;
                    }
                }
            }
        }

        float const speed = std::max(1.0f, me->GetSpeed(MOVE_RUN));
        ProposalTier const tier = dangerous ? ProposalTier::Urgent : ProposalTier::Assigned;

        // 2. Run to the tank: it takes the enemy over there.
        PartyMemberFacts const* tank = nullptr;
        for (PartyMemberFacts const* t : Tanks::Alive(world))
        {
            if (t->guid == self->guid || !t->CanAct())
                continue;
            if (!tank || Distance2d(here, t->pos) < Distance2d(here, tank->pos))
                tank = t;
        }
        if (tank)
        {
            float const distance = Distance2d(here, tank->pos);
            if (distance <= START_DISTANCE || distance > MAX_RUN_DISTANCE)
                return;

            SafeRoute::IdleCreatures const idle(me);
            Position3 to;
            float const k = STAND_OFF / distance;
            to.x = tank->pos.x + (here.x - tank->pos.x) * k;
            to.y = tank->pos.y + (here.y - tank->pos.y) * k;
            to.z = tank->pos.z;
            char const* reason = "melee enemy on me: run to the tank";
            if (idle.Near(to) || !SafeRoute::PathIsSafe(me, idle, to, MAX_PATH_DETOUR))
            {
                // FleePoint prefers the tank too, else the party, else a ring; never an idle pack.
                if (!FleePoint::Find(me, chasers.front().unit, &world, to.x, to.y, to.z))
                    return;
                reason = "melee enemy on me: way to the tank blocked, run to a safe spot";
            }

            float const walkSec = Distance2d(here, to) * PATH_FACTOR / speed;
            float benefit = 0.0f;
            for (Chaser const& c : chasers)
            {
                float const horizon = std::max(1.0f, std::min(HORIZON_SEC, Value::EnemyTimeToDie(world, *c.facts)));
                auto const loss = [&](float seconds)
                {
                    return Value::MoveLoss(world, *c.facts, *self, *tank, seconds) +
                           Value::HarassLoss(world, *c.facts, *self, seconds);
                };
                benefit += loss(horizon) - loss(std::min(horizon, walkSec + TAUNT_REACTION_SEC));
            }
            Proposal p = MakeProposal(tier, reason);
            SetMove(p, to, walkSec);
            p.SetValue(benefit, Value::LostAction(world, *self, walkSec));
            out.push_back(p);
            return;
        }

        // 3. Nobody to take it: only get away while it is rooted (the old Nova-and-run).
        if (!allRooted)
            return;
        Position3 to;
        if (!FleePoint::Find(me, chasers.front().unit, &world, to.x, to.y, to.z))
            return;
        float const walkSec = Distance2d(here, to) * PATH_FACTOR / speed;
        Proposal p = MakeProposal(tier, "enemies rooted, no tank: run to a safe spot");
        SetMove(p, to, walkSec);
        p.SetValue(SavedWhileAway(world, chasers, *self, ROOT_HOLD_SEC), Value::LostAction(world, *self, walkSec));
        out.push_back(p);
    }
}
