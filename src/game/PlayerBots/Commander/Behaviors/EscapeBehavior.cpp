#include "Commander/Behaviors/EscapeBehavior.h"
#include "Commander/Classes/ClassKit.h"
#include "Commander/Behaviors/FleePoint.h"
#include "Commander/Behaviors/SafeRoute.h"
#include "Commander/Perception/Tanks.h"
#include "Commander/Perception/ValueModel.h"
#include "Commander/BotDirectives.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/ThreatView.h"
#include "Commander/Perception/ControlValue.h"
#include "Commander/Perception/FightLoad.h"
#include "PartyBotAI.h"
#include "SpellAuras.h"
#include "SpellMgr.h"
#include "DBCStores.h"
#include "Map.h"
#include "SpellEntry.h"
#include <algorithm>
#include <cmath>
#include <cstring>

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

        // The run this module started (MoveTo, BotDirectives::GetMovementOwner) is still under
        // way: stop it where the bot is. False = not walking, or somebody else's walk.
        bool ProposeStopOwnRun(PartyBotAI const* bot, char const* reason, std::vector<Proposal>& out)
        {
            if (!bot->me->IsMoving())
                return false;
            char const* owner = bot->Directives().GetMovementOwner(WorldTimer::getMSTime());
            if (!owner || strcmp(owner, "escape") != 0)
                return false;
            Proposal p;
            p.tier = ProposalTier::Assigned;
            p.source = "escape";
            p.reason = reason;
            // Above the "keep walking" Holds (score 0) other modules give for any MoveTo under
            // way: this walk is ours, they only keep it from being cut by a cast.
            p.score = 0.01f;
            p.action.type = BotActionType::StopMove;
            out.push_back(p);
            return true;
        }

        // Spells that stop a chaser where it stands come from the class kit
        // (ClassKit::HoldSpells); what they do (root / stun, area or single target, duration,
        // breaks on damage) is read from the spell data in HoldSec / ProposeHold.

        // Seconds `spell` keeps an enemy from reaching the bot: a stun for its duration, a root
        // (or control that ends on the next hit) at most ROOT_HOLD_SEC: other members' damage and
        // area spells break it. 0 = not a root / stun (fear, slow: not used, a feared enemy runs
        // into the next pack).
        float HoldSec(SpellEntry const* spell)
        {
            bool root = false, stun = false;
            for (uint32 i = 0; i < MAX_SPELL_EFFECTS; ++i)
            {
                if (spell->Effect[i] != SPELL_EFFECT_APPLY_AURA)
                    continue;
                root = root || spell->EffectApplyAuraName[i] == SPELL_AURA_MOD_ROOT;
                stun = stun || spell->EffectApplyAuraName[i] == SPELL_AURA_MOD_STUN;
            }
            float const sec = std::max(0, spell->GetDuration()) / 1000.0f;
            if (stun && !spell->HasAuraInterruptFlag(AURA_INTERRUPT_DAMAGE_CANCELS))
                return sec;
            if (root || stun)
                return std::min(sec, EB::ROOT_HOLD_SEC);
            return 0.0f;
        }

        float SpellRadius(SpellEntry const* spell)
        {
            float radius = 0.0f;
            for (uint32 i = 0; i < MAX_SPELL_EFFECTS; ++i)
                if (SpellRadiusEntry const* r = sSpellRadiusStore.LookupEntry(spell->EffectRadiusIndex[i]))
                    radius = std::max(radius, r->Radius);
            return radius;
        }

        // Health the bot's paid-up shields will still take: Power Word: Shield, Ice Barrier.
        // Hits they absorb cost nothing more: no health and no spell pushback (Unit::DealDamage
        // delays a cast only for damage that got through).
        float FreeAbsorb(PartyBotAI const* bot)
        {
            float absorb = 0.0f;
            for (Aura const* aura : bot->me->GetAurasByType(SPELL_AURA_SCHOOL_ABSORB))
                absorb += std::max(0.0f, aura->GetModifier()->m_amount);
            return absorb;
        }

        // FreeAbsorb plus Mana Shield as far as the mana lasts (it costs mana per point, so it
        // counts for surviving, not as free).
        float ActiveAbsorb(PartyBotAI const* bot)
        {
            Player* me = bot->me;
            float absorb = FreeAbsorb(bot);
            for (Aura const* aura : me->GetAurasByType(SPELL_AURA_MANA_SHIELD))
            {
                float amount = std::max(0.0f, aura->GetModifier()->m_amount);
                if (float const perPoint = aura->GetSpellProto()->EffectMultipleValue[aura->GetEffIndex()])
                    amount = std::min(amount, float(me->GetPower(POWER_MANA)) / perPoint);
                absorb += amount;
            }
            return absorb;
        }

        // The class kit shield (ClassKit::ReactiveShields) castable now that absorbs most,
        // `amountOut` capped by the mana for a mana-paid one (Mana Shield). Null = none.
        SpellEntry const* BestCastableShield(PartyBotAI const* bot, float& amountOut)
        {
            Player* me = bot->me;
            SpellEntry const* best = nullptr;
            amountOut = 0.0f;
            for (SpellEntry const* shield : bot->Kit().ReactiveShields())
            {
                if (!shield || !bot->CanTryToCastSpell(me, shield))
                    continue;
                float amount = float(std::max(0, shield->CalculateSimpleValue(EFFECT_INDEX_0)));
                if (float const perPoint = shield->EffectMultipleValue[EFFECT_INDEX_0])
                    amount = std::min(amount, float(me->GetPower(POWER_MANA)) / perPoint);
                if (amount > amountOut)
                {
                    best = shield;
                    amountOut = amount;
                }
            }
            return best;
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

        // Cast `spell` (HoldSpells) on the chasers it can hold: an area around the bot (Frost
        // Nova) takes every free chaser in its radius, a single target spell the one it saves
        // most on. False = not castable / nobody to hold.
        bool ProposeHold(PartyBotAI const* bot, WorldModel const& world, PartyMemberFacts const& self,
                         std::vector<Chaser> const& chasers, SpellEntry const* spell, Proposal& p)
        {
            if (!spell)
                return false;
            float const hold = HoldSec(spell);
            if (hold <= 0.0f)
                return false;
            Player* me = bot->me;
            float const castSec = std::max(Value::GCD_SEC, spell->GetCastTime(me) / 1000.0f);
            float const cost = Value::LostAction(world, self, castSec);

            std::vector<Chaser> held;
            ObjectGuid target;
            if (spell->rangeIndex == SPELL_RANGE_IDX_SELF_ONLY)
            {
                if (!bot->CanTryToCastSpell(me, spell))
                    return false;
                float const radius = SpellRadius(spell);
                for (Chaser const& c : chasers)
                    if (!c.rooted && me->IsWithinDistInMap(c.unit, radius))
                        held.push_back(c);
            }
            else
            {
                float best = 0.0f;
                for (Chaser const& c : chasers)
                {
                    if (c.rooted || !bot->CanTryToCastSpell(c.unit, spell) ||
                        (spell->TargetCreatureType && !(c.unit->GetCreatureTypeMask() & spell->TargetCreatureType)))
                        continue;
                    float const saved = SavedWhileAway(world, { c }, self, hold);
                    if (held.empty() || saved > best)
                    {
                        held = { c };
                        best = saved;
                        target = c.facts->guid;
                    }
                }
            }
            if (held.empty())
                return false;

            p = MakeProposal(ProposalTier::Urgent, "dangerous hits: root / stun them");
            p.action.type = BotActionType::Cast;
            p.action.target = target;
            p.action.spell = spell;
            p.SetValue(SavedWhileAway(world, held, self, hold), cost);
            return true;
        }

        // An area fear around the bot from its ClassKit::Controls (Psychic Scream, Howl of
        // Terror), valued by ControlValue (user, 2026-11-07: a priest saving itself when
        // everything runs at it counts; fear's add risk and scatter are in the worth). The
        // fear does not change threat: they come back to the bot, the worth only counts what
        // happens until the tank would have them (ControlValue::TAKEOVER_SEC).
        bool ProposeSelfFear(PartyBotAI const* bot, WorldModel const& world, PartyMemberFacts const& self, Proposal& p)
        {
            FightLoad::Input fight;
            bool haveFight = false;
            bool found = false;
            for (ControlAbility const& a : self.controls)
            {
                if (!a.ready || !a.fear || a.radius <= 0.0f)
                    continue;
                SpellEntry const* spell = sSpellMgr.GetSpellEntry(a.spellId);
                if (!spell || spell->rangeIndex != SPELL_RANGE_IDX_SELF_ONLY || !bot->CanTryToCastSpell(bot->me, spell))
                    continue;
                std::vector<EnemyFacts const*> const targets = ControlValue::Targets(world, self, a, nullptr);
                if (targets.empty())
                    continue;
                if (!haveFight && !(haveFight = FightLoad::FromWorld(world, fight)))
                    return false;
                ControlValue::Worth const w = ControlValue::Of(world, fight, self, a, targets, 0.0f);
                if (found && w.Net() <= p.score)
                    continue;
                p = MakeProposal(ProposalTier::Urgent, "dangerous hits: fear them off");
                p.action.type = BotActionType::Cast;
                p.action.spell = spell;
                p.SetValue(w.Benefit(), w.Cost());
                found = true;
            }
            return found;
        }
    }

    bool EscapeBehavior::AppliesTo(PartyBotAI const* bot) const
    {
        return bot->GetRole() == ROLE_HEALER || bot->GetRole() == ROLE_RANGE_DPS;
    }

    bool EscapeBehavior::TakesOver(Takeover what, PartyBotAI const* bot, WorldModel const& /*world*/) const
    {
        return what == Takeover::Escape && AppliesTo(bot) && !bot->IsInDuel();
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
        // Nobody chases any more (the tank took it): no reason to keep running.
        if (chasers.empty())
        {
            ProposeStopOwnRun(bot, "nobody chasing any more, stop", out);
            return;
        }

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

        // Danger: the bot dies (health + active shields) before the chasers do, and within
        // DANGER_SEC. Two weak bugs that die in a few seconds are not dangerous: stay, fight.
        float const incoming = Value::IncomingOn(world, self->guid);
        float killSec = 0.0f;
        for (Chaser const& c : chasers)
            killSec = std::max(killSec, Value::EnemyTimeToDie(world, *c.facts));
        float const absorb = ActiveAbsorb(bot);
        float const surviveSec = incoming > 0.0f ? (float(self->health) + absorb) / incoming : ThreatView::NO_LIMIT;
        bool const dangerous = surviveSec < DANGER_SEC && surviveSec < killSec;
        bool const allHeld = std::all_of(chasers.begin(), chasers.end(), [](Chaser const& c) { return c.rooted; });
        // Seconds of the hits the free shields still take (Ice Barrier, Power Word: Shield):
        // no damage and no pushback until then, so getting away saves only what comes after
        // (strat-20261102-205906: the mage under Ice Barrier ran from a Skeletal Guardian
        // hitting for ~50 and took 0 damage the whole fight).
        float const absorbedSec = incoming > 0.0f ? FreeAbsorb(bot) / incoming : 0.0f;

        // 0. Dangerous and nothing absorbs: the class's shield first (instant). Mana Shield
        // costs 2 mana per point absorbed, so the rotation never casts it by itself
        // (user, 2026-11-01: only when it is really dangerous); next tick the danger is judged
        // again with the shield up. Benefit: the hits it takes within DANGER_SEC.
        if (dangerous && absorb <= 0.0f)
        {
            float amount = 0.0f;
            if (SpellEntry const* shield = BestCastableShield(bot, amount))
            {
                Proposal p = MakeProposal(ProposalTier::Urgent, "dangerous hits: shield up");
                p.action.type = BotActionType::Cast;
                p.action.spell = shield;
                p.SetValue(Value::Points(world, std::min(amount, incoming * DANGER_SEC)),
                           Value::LostAction(world, *self, Value::GCD_SEC));
                out.push_back(p);
                return;
            }
        }

        // 1. Hold them first when the hits are dangerous: the best of the class's roots / stuns,
        // or an area fear around the bot (Psychic Scream) valued like the brain's controls.
        if (dangerous && !allHeld)
        {
            Proposal best;
            bool found = false;
            for (SpellEntry const* spell : bot->Kit().HoldSpells())
            {
                Proposal p;
                if (ProposeHold(bot, world, *self, chasers, spell, p) && (!found || p.score > best.score))
                {
                    best = p;
                    found = true;
                }
            }
            Proposal fear;
            if (ProposeSelfFear(bot, world, *self, fear) && fear.score > 0.0f && (!found || fear.score > best.score))
            {
                best = fear;
                found = true;
            }
            if (found)
            {
                out.push_back(best);
                return;
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
                // Loss of `seconds` more on me: the first absorbedSec of it costs nothing.
                auto const loss = [&](float seconds)
                {
                    seconds = std::max(0.0f, seconds - absorbedSec);
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
        if (!allHeld)
            return;
        Position3 to;
        if (!FleePoint::Find(me, chasers.front().unit, &world, to.x, to.y, to.z))
            return;
        float const walkSec = Distance2d(here, to) * PATH_FACTOR / speed;
        Proposal p = MakeProposal(tier, "enemies rooted, no tank: run to a safe spot");
        SetMove(p, to, walkSec);
        p.SetValue(SavedWhileAway(world, chasers, *self, std::max(0.0f, ROOT_HOLD_SEC - absorbedSec)),
                   Value::LostAction(world, *self, walkSec));
        out.push_back(p);
    }
}
