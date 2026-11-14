#include "Commander/Behaviors/Arbiter.h"
#include "Commander/Behaviors/Constraints.h"
#include "Commander/Behaviors/CurrentAction.h"
#include "Commander/BotDirectives.h"
#include "Commander/Commands/BotSpeech.h"
#include "Commander/CommanderMgr.h"
#include "PartyBotAI.h"
#include "GameObject.h"
#include "Map.h"
#include "MotionMaster.h"
#include "Pet.h"
#include "Spell.h"
#include "SpellEntry.h"
#include "Timer.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace Commander
{
    namespace
    {
        char const* ActionName(BotActionType type)
        {
            switch (type)
            {
                case BotActionType::Cast:   return "cast";
                case BotActionType::Attack: return "attack";
                case BotActionType::MoveTo: return "move";
                case BotActionType::StopMove: return "stop";
                case BotActionType::Backpedal: return "backpedal";
                case BotActionType::Chase:  return "chase";
                case BotActionType::StopChase: return "stop-chase";
                case BotActionType::Hold:   return "hold";
                case BotActionType::Rest:   return "rest";
                case BotActionType::HoldFire: return "holdfire";
                case BotActionType::Retreat: return "retreat";
                case BotActionType::UseObject: return "use";
                case BotActionType::PetAttack: return "pet-attack";
                case BotActionType::PetFollow: return "pet-follow";
                case BotActionType::PetAutocastOn: return "pet-autocast-on";
                case BotActionType::PetAutocastOff: return "pet-autocast-off";
            }
            return "?";
        }

        // Every module walk runs at run speed. MoveSplineInit takes walk mode from the movement
        // flags when the order does not say (MOVEFLAG_WALK_MODE stays after FollowMovementGenerator's
        // last short step, < 2 yd, or a chase spread / back step), so a plain MovePoint after one
        // walked at 2.5 yd/s (strat-20261108-174726: the priest crossed the fight walking).
        void RunTo(Player* me, float x, float y, float z)
        {
            me->GetMotionMaster()->MovePoint(0, x, y, z, MOVE_PATHFINDING | MOVE_RUN_MODE);
        }

        bool IsPetAction(BotActionType type)
        {
            switch (type)
            {
                case BotActionType::PetAttack:
                case BotActionType::PetFollow:
                case BotActionType::PetAutocastOn:
                case BotActionType::PetAutocastOff:
                    return true;
                default:
                    return false;
            }
        }

        // Moves the pet: at most one per tick. Autocast toggles do not conflict with each other.
        bool IsPetCommand(BotActionType type)
        {
            return type == BotActionType::PetAttack || type == BotActionType::PetFollow;
        }

        // The original rotation still runs in the tick the action executed (Arbiter::Run).
        bool LetsRotationRun(BotActionType type)
        {
            return type == BotActionType::Attack || type == BotActionType::Backpedal ||
                   type == BotActionType::StopMove || type == BotActionType::Chase ||
                   type == BotActionType::StopChase;
        }

        // Arbiter.h "Two channels".
        enum class Channel : uint8 { Move, Act, Exclusive };

        Channel ChannelOf(BotActionType type)
        {
            switch (type)
            {
                case BotActionType::MoveTo:
                case BotActionType::Backpedal:
                case BotActionType::Chase:
                    return Channel::Move;
                case BotActionType::Cast:
                case BotActionType::Attack:
                case BotActionType::HoldFire:
                    return Channel::Act;
                default:
                    return Channel::Exclusive;
            }
        }

        // `act` can be done while `move` goes on: an instant cast that does not need the bot
        // to stop (no cast time, not a channel moving ends, not an auto shot; a spell that must
        // face its target only when the bot faces it already: DoCastSpell would stop the walk
        // to turn), or a target switch / stop hitting that is not against a chase (a chase
        // attacks its own target).
        bool GoTogether(PartyBotAI const* bot, Proposal const& move, Proposal const& act)
        {
            Player* me = bot->me;
            switch (act.action.type)
            {
                case BotActionType::Cast:
                {
                    SpellEntry const* spell = act.action.spell;
                    if (!spell || CurrentAction::NeedsStandingStill(bot, spell) || spell->IsAutoRepeatRangedSpell())
                        return false;
                    // Turned first (Blink): the walk would turn it back.
                    if (act.action.facing >= 0.0f)
                        return false;
                    if (act.action.target.IsEmpty() || act.action.target == me->GetObjectGuid() || !spell->IsNeedFaceTarget())
                        return true;
                    Unit const* target = me->GetMap()->GetUnit(act.action.target);
                    return target && me->IsFacingTarget(target);
                }
                case BotActionType::Attack:
                case BotActionType::HoldFire:
                    return move.action.type != BotActionType::Chase;
                default:
                    return false;
            }
        }

        // Highest tier first, then the highest score inside the tier.
        bool IsBetter(Proposal const& a, Proposal const& b)
        {
            if (a.tier != b.tier)
                return a.tier < b.tier;
            return a.score > b.score;
        }

        char const* VetoName(VetoType type)
        {
            switch (type)
            {
                case VetoType::None:   return "none";

                case VetoType::Heal:   return "no-heal";
            }
            return "?";
        }

        std::string TargetName(PartyBotAI const* bot, ObjectGuid guid)
        {
            if (guid.IsEmpty())
                return "self";
            if (guid.IsGameObject())
            {
                if (GameObject* go = bot->me->GetMap()->GetGameObject(guid))
                    return go->GetName();
                return guid.GetString();
            }
            if (Unit* unit = bot->me->GetMap()->GetUnit(guid))
                return unit->GetName();
            return guid.GetString();
        }

        CandidateRecord MakeRecord(std::string what, Proposal const& p)
        {
            CandidateRecord r;
            r.source = p.source;
            r.what = std::move(what);
            r.kind = p.kind;
            r.tier = uint8(p.tier);
            r.score = p.score;
            r.valued = p.valued;
            r.benefit = p.benefit;
            r.cost = p.cost;
            r.reason = p.reason;
            if (p.interruptCost > 0.0f)
            {
                char buf[96];
                snprintf(buf, sizeof(buf), " [cost +%.2f: %s]", p.interruptCost, p.interrupts);
                r.reason += buf;
            }
            return r;
        }
    }

    std::string Arbiter::Describe(PartyBotAI const* bot, Proposal const& p)
    {
        char buf[200];
        if (p.kind == ProposalKind::Veto)
        {
            snprintf(buf, sizeof(buf), "%s %s spell%u", VetoName(p.veto.type),
                TargetName(bot, p.veto.target).c_str(), p.veto.spellId);
            return buf;
        }
        snprintf(buf, sizeof(buf), "%s %s spell%u", p.kind == ProposalKind::HealOrder ? "heal" : ActionName(p.action.type),
            TargetName(bot, p.action.target).c_str(), p.action.spell ? p.action.spell->Id : 0);
        return buf;
    }

    bool Arbiter::Run(PartyBotAI* bot, WorldModel const& world, TeamPlan const& plan,
                      BehaviorList const& behaviors)
    {
        std::vector<IBehavior const*> active;
        for (auto const& slot : behaviors)
            if (slot->enabled.load(std::memory_order_relaxed) && slot->module->AppliesTo(bot))
                active.push_back(slot->module.get());

        if (active.empty())
            return false;

        uint32 const now = WorldTimer::getMSTime();
        BotDirectives& directives = bot->Directives();

        // Decisions modules take over from the class rotation (taunts, escapes, interrupts,
        // heals, Fade). Renewed every tick; they expire by themselves if the module is disabled
        // or the Commander is turned off. A heal order of the last tick is dropped below: no
        // order now = no heal (rest).
        for (size_t i = 0; i < size_t(Takeover::Count); ++i)
        {
            Takeover const what = Takeover(i);
            for (IBehavior const* b : active)
            {
                if (b->TakesOver(what, bot, world, plan))
                {
                    directives.TakeOver(what, now, TAKEOVER_MS);
                    break;
                }
            }
        }
        directives.ClearHealOrder();

        std::vector<Proposal> proposals;
        for (IBehavior const* b : active)
            b->Propose(bot, world, plan, proposals);

        if (proposals.empty())
            return false;

        for (IBehavior const* b : active)
            b->Adjust(bot, world, proposals);

        // What the bot is doing now has a value (Behaviors/CurrentAction.h): a valued proposal
        // that ends the running cast, channel or walk pays for it. Unvalued ones are orders.
        CurrentAction::State const current = CurrentAction::Read(bot, world, plan);
        bool const anyCurrent = current.Casting() || current.walkOwner;
        if (anyCurrent)
        {
            for (Proposal& p : proposals)
            {
                if (p.kind != ProposalKind::Replace || !p.valued || IsPetAction(p.action.type))
                    continue;
                char const* what = "";
                float const extra = CurrentAction::InterruptCost(bot, current, p.action, what);
                if (extra <= 0.0f)
                    continue;
                p.interruptCost = extra;
                p.interrupts = what;
                p.cost += extra;
                p.score = p.benefit - p.cost;
            }
        }

        DecisionRecord record;
        record.timeMs = now;

        // Value-scored proposals that are not worth it, and vetoes.
        std::vector<Proposal> replace;
        std::vector<Proposal> petActions;
        std::vector<Proposal> healOrders;
        uint32 const vetoMs = VETO_MS;
        for (Proposal const& p : proposals)
        {
            // The brain's constraints, whichever module proposed it (Constraints.h).
            if (char const* constraint = Constraints::Broken(bot, world, plan, p, now))
            {
                record.candidates.push_back(MakeRecord(Describe(bot, p), p));
                CandidateRecord& r = record.candidates.back();
                r.result = CandidateRecord::Result::Forbidden;
                r.reason = std::string(constraint) + " (" + p.reason + ")";
                continue;
            }
            if (p.valued && p.benefit <= p.cost)
            {
                record.candidates.push_back(MakeRecord(Describe(bot, p), p));
                record.candidates.back().result = CandidateRecord::Result::Dropped;
                continue;
            }
            if (p.kind == ProposalKind::Veto)
            {
                directives.AddVeto(p.veto, now, vetoMs);
                record.candidates.push_back(MakeRecord(Describe(bot, p), p));
                record.candidates.back().result = CandidateRecord::Result::VetoSet;
                continue;
            }
            if (p.kind == ProposalKind::HealOrder)
            {
                healOrders.push_back(p);
                continue;
            }
            if (IsPetAction(p.action.type))
            {
                petActions.push_back(p);
                continue;
            }
            replace.push_back(p);
        }

        std::stable_sort(replace.begin(), replace.end(), IsBetter);
        std::stable_sort(petActions.begin(), petActions.end(), IsBetter);
        std::stable_sort(healOrders.begin(), healOrders.end(), IsBetter);

        // The best heal order waits for the rotation's heal step (this tick, unless a Replace
        // below executes; the order is proposed again next tick then). Its lifetime stays one
        // bot interval on purpose: it is an order for this tick only (cleared at the start of
        // every run), not a decision kept between ticks like takeovers and vetoes.
        for (size_t i = 0; i < healOrders.size(); ++i)
        {
            record.candidates.push_back(MakeRecord(Describe(bot, healOrders[i]), healOrders[i]));
            if (i == 0)
            {
                directives.SetHealOrder(healOrders[i].action, healOrders[i].tier <= ProposalTier::Urgent,
                                        now, sCommanderMgr.GetBotUpdateInterval());
                record.candidates.back().result = CandidateRecord::Result::OrderSet;
            }
        }
        // A heal order and a Replace compete for the same tick (a Replace skips the rotation and
        // with it the heal step): one in a lower tier, or lower in score in the same tier, than
        // the heal order stands down. Attack / Backpedal let the rotation run, they are kept.
        Proposal const* bestHeal = healOrders.empty() ? nullptr : &healOrders.front();

        bool executed = false;
        bool endedCurrent = false;
        bool rotationMayRun = false;
        size_t const firstReplaceRecord = record.candidates.size();
        Proposal const* primary = nullptr;
        if (!replace.empty())
        {
            ProposalTier const topTier = replace.front().tier;
            for (Proposal const& p : replace)
            {
                record.candidates.push_back(MakeRecord(Describe(bot, p), p));
                CandidateRecord& r = record.candidates.back();

                // Lower tiers are only considered when nothing in the top tier exists,
                // not when the top tier failed to execute.
                if (p.tier != topTier)
                {
                    r.result = CandidateRecord::Result::LowerTier;
                    continue;
                }
                if (executed)
                    continue;   // NotTried
                if (bestHeal && !LetsRotationRun(p.action.type) && IsBetter(*bestHeal, p))
                {
                    r.result = bestHeal->tier != p.tier ? CandidateRecord::Result::LowerTier
                                                        : CandidateRecord::Result::NotTried;
                    continue;
                }

                if (Execute(bot, p))
                {
                    executed = true;
                    endedCurrent = p.interruptCost > 0.0f;
                    // A target switch only changes the victim: the original rotation can act on
                    // the new target in the same tick. Otherwise a tank switching between loose
                    // adds every tick never casts Thunder Clap or Sunder (combat scenario 9).
                    // Walking backwards is the same: the tank keeps hitting and building threat.
                    rotationMayRun = LetsRotationRun(p.action.type);
                    r.result = CandidateRecord::Result::Executed;
                    directives.SetLastDecision(now, std::string(p.source) + " " + r.what);
                    directives.NoteExecuted(p, now);
                    BotSpeech::OnExecuted(bot, p, now);
                    primary = &p;
                }
                else
                    r.result = CandidateRecord::Result::Failed;
            }
            if (!executed && !(bestHeal && IsBetter(*bestHeal, replace.front())))
                directives.SetLastDecision(now, "failed: " + std::string(replace.front().source) + " " + Describe(bot, replace.front()));
        }

        // The other channel (Arbiter.h "Two channels"): the executed proposal moves the bot or
        // acts; the best proposal of the other channel that goes with it runs too (the tank's
        // shout or target switch while walking backwards to its corner; strat-20261108-200046:
        // the walk won every tick and the shout never went out). Its own best tier, not the
        // winner's: moving and acting do not take from each other.
        if (primary && primary->tier != ProposalTier::Override && ChannelOf(primary->action.type) != Channel::Exclusive)
        {
            Channel const want = ChannelOf(primary->action.type) == Channel::Move ? Channel::Act : Channel::Move;
            bool tierSet = false;
            ProposalTier secondTier = ProposalTier::Routine;
            for (size_t i = 0; i < replace.size(); ++i)
            {
                Proposal const& p = replace[i];
                CandidateRecord& r = record.candidates[firstReplaceRecord + i];
                if (&p == primary || ChannelOf(p.action.type) != want ||
                    (r.result != CandidateRecord::Result::NotTried && r.result != CandidateRecord::Result::LowerTier))
                    continue;
                if (tierSet && p.tier != secondTier)
                    break;      // a lower tier of this channel: only when its top tier has none
                Proposal const& move = want == Channel::Move ? p : *primary;
                Proposal const& act = want == Channel::Act ? p : *primary;
                if (!GoTogether(bot, move, act))
                    continue;
                if (bestHeal && !LetsRotationRun(p.action.type) && IsBetter(*bestHeal, p))
                    continue;
                tierSet = true;
                secondTier = p.tier;
                if (!Execute(bot, p))
                {
                    r.result = CandidateRecord::Result::Failed;
                    continue;
                }
                r.result = CandidateRecord::Result::Executed;
                r.reason += " [with the ";
                r.reason += want == Channel::Act ? "walk]" : "action]";
                rotationMayRun = rotationMayRun && LetsRotationRun(p.action.type);
                endedCurrent = endedCurrent || p.interruptCost > 0.0f;
                directives.SetLastDecision(now, std::string(move.source) + " " + Describe(bot, move) + " + " +
                                                std::string(act.source) + " " + Describe(bot, act));
                // The action last: CurrentAction reads the last executed cast (its order's worth).
                directives.NoteExecuted(move, now);
                directives.NoteExecuted(act, now);
                BotSpeech::OnExecuted(bot, p, now);
                break;
            }
        }

        // Pet actions, besides the bot's own action: every autocast toggle, and the best pet
        // command that executes. During a retreat RetreatBehavior calls the pet back itself.
        bool const retreating = !replace.empty() && replace.front().tier == ProposalTier::Override;
        bool petCommanded = false;
        for (Proposal const& p : petActions)
        {
            record.candidates.push_back(MakeRecord(Describe(bot, p), p));
            CandidateRecord& r = record.candidates.back();
            bool const command = IsPetCommand(p.action.type);
            if (command && retreating)
            {
                r.result = CandidateRecord::Result::LowerTier;
                continue;
            }
            if (command && petCommanded)
                continue;   // NotTried
            if (Execute(bot, p))
            {
                r.result = CandidateRecord::Result::Executed;
                petCommanded = petCommanded || command;
            }
            else
                r.result = CandidateRecord::Result::Failed;
        }

        // The running action as a candidate of its own, so the log shows what it was worth.
        if (anyCurrent)
        {
            CandidateRecord r;
            char buf[64];
            if (current.Casting())
            {
                snprintf(buf, sizeof(buf), "continue spell%u", current.spell->Id);
                r.source = current.keepFrom;
                r.reason = current.keepReason;
                r.benefit = current.keep;
            }
            else
            {
                snprintf(buf, sizeof(buf), "keep walking");
                r.source = current.walkOwner;
                r.reason = "the walk's order";
                r.benefit = current.walkKeep;
            }
            r.what = buf;
            r.valued = true;
            r.score = r.benefit;
            r.result = endedCurrent ? CandidateRecord::Result::Ended : CandidateRecord::Result::Kept;
            record.candidates.push_back(std::move(r));
        }

        directives.RecordDecision(std::move(record));
        return executed && !rotationMayRun;
    }

    bool Arbiter::Execute(PartyBotAI* bot, Proposal const& proposal)
    {
        Player* me = bot->me;
        BotAction const& action = proposal.action;

        // A resting bot (RestBehavior) gets up for anything else. Pet actions are not the bot's own.
        if (action.type == BotActionType::Rest)
            return bot->StartResting();
        if (!IsPetAction(action.type) && !me->IsStandingUp())
            me->SetStandState(UNIT_STAND_STATE_STAND);

        // The target is a gameobject, not a unit: handled before the unit lookup below.
        if (action.type == BotActionType::UseObject)
            return ExecuteUseObject(bot, action);

        Unit* pTarget = nullptr;
        if (!action.target.IsEmpty())
        {
            pTarget = me->GetMap()->GetUnit(action.target);
            if (!pTarget || !pTarget->IsInWorld() || !pTarget->IsAlive())
                return false;
        }

        switch (action.type)
        {
            case BotActionType::Cast:
            {
                if (!action.spell)
                    return false;
                if (action.atPoint)
                    return bot->DoCastAt(action.x, action.y, action.z, action.spell) == SPELL_CAST_OK;
                if (action.facing >= 0.0f)
                {
                    if (action.jumpTurn && bot->OrderJumpCast(action.spell, action.facing))
                        return true;
                    return bot->DoCastFacing(action.facing, action.spell) == SPELL_CAST_OK;
                }
                Unit* pCastTarget = pTarget ? pTarget : me;
                // A warrior swaps stance first when needed (Thunder Clap or Mocking Blow from
                // Defensive Stance): PartyBotAI::DoCastWithStance, doc 2026-11-01-warrior-tank.md.
                if (!bot->CanCastWithStance(pCastTarget, action.spell))
                    return false;
                if (!bot->CastPrepSpell(action.prepSpell))
                    return false;
                // Fix: an auto-repeat shot (Auto Shot, ranged pull) cast on a unit that is not the
                // bot's victim is cancelled by PartyBotAI::UpdateAI on the next tick ("stop auto
                // shot if no target") before its 0.5 s wind-up ends when the bot ticks faster than
                // that. Set the victim without melee (Unit::Attack, meleeAttack = false: no chase,
                // no combat by itself) so the shot is kept.
                if (action.spell->IsAutoRepeatRangedSpell() && pTarget && me->GetVictim() != pTarget &&
                    bot->IsValidHostileTarget(pTarget))
                {
                    if (me->IsMounted())
                        me->RemoveSpellsCausingAura(SPELL_AURA_MOUNTED);
                    me->Attack(pTarget, false);
                }
                if (bot->DoCastWithStance(pCastTarget, action.spell) != SPELL_CAST_OK)
                    return false;
                if (action.dropTarget && pTarget && me->GetVictim() == pTarget)
                    me->AttackStop();
                return true;
            }
            case BotActionType::Attack:
            {
                if (!pTarget || me->GetVictim() == pTarget || !bot->IsValidHostileTarget(pTarget))
                    return false;
                if (me->IsMounted())    // Unit::Attack refuses while mounted
                    me->RemoveSpellsCausingAura(SPELL_AURA_MOUNTED);
                if (!bot->AttackStart(pTarget))
                    return false;
                // Fix: Unit::Attack does not retarget a running Auto Shot / wand, and the rotations
                // only start one when none is active, so a hunter or wand user kept shooting the old
                // target (also the one it is about to pull aggro from). Stop it; the rotation starts
                // it again on the new victim (this tick for hunters, next tick for wands).
                if (Spell* autoRepeat = me->GetCurrentSpell(CURRENT_AUTOREPEAT_SPELL))
                    if (autoRepeat->m_targets.getUnitTargetGuid() != pTarget->GetObjectGuid())
                        me->InterruptSpell(CURRENT_AUTOREPEAT_SPELL, true);
                return true;
            }
            case BotActionType::MoveTo:
            {
                if (me->HasUnitState(UNIT_STATE_ROOT))
                    return false;
                RunTo(me, action.x, action.y, action.z);
                bot->Directives().HoldMovement(WorldTimer::getMSTime(), action.holdMs, proposal.source);
                return true;
            }
            case BotActionType::StopMove:
            {
                // Only a module's own walk (BotActionType::StopMove): another module's MoveTo
                // or a player command started since then is left alone.
                BotDirectives& directives = bot->Directives();
                char const* owner = directives.GetMovementOwner(WorldTimer::getMSTime());
                if (!owner || !proposal.source || strcmp(owner, proposal.source) != 0)
                    return false;
                directives.ReleaseMovement();
                if (!me->IsStopped())
                    me->StopMoving();
                me->GetMotionMaster()->Clear(false, true);
                me->GetMotionMaster()->MoveIdle();
                return true;
            }
            case BotActionType::Backpedal:
            {
                Position3 to;
                to.x = action.x;
                to.y = action.y;
                to.z = action.z;
                if (!bot->OrderBackpedal(to, action.target, action.holdMs))
                    return false;
                // Original follow / chase off while walking (the order is renewed every tick).
                bot->Directives().HoldMovement(WorldTimer::getMSTime(), action.holdMs);
                return true;
            }
            case BotActionType::Chase:
            {
                if (!pTarget || !bot->IsValidHostileTarget(pTarget))
                    return false;
                if (me->GetVictim() != pTarget)
                {
                    if (me->IsMounted())    // Unit::Attack refuses while mounted
                        me->RemoveSpellsCausingAura(SPELL_AURA_MOUNTED);
                    if (!bot->AttackStart(pTarget))
                        return false;
                }
                return bot->OrderChase(pTarget, action.x);
            }
            case BotActionType::StopChase:
            {
                if (me->GetMotionMaster()->GetCurrentMovementGeneratorType() != CHASE_MOTION_TYPE)
                    return false;
                if (!me->IsStopped())
                    me->StopMoving();
                me->GetMotionMaster()->Clear(false, true);
                me->GetMotionMaster()->MoveIdle();
                return true;
            }
            case BotActionType::Hold:
                return true;
            case BotActionType::HoldFire:
            {
                if (me->GetCurrentSpell(CURRENT_AUTOREPEAT_SPELL))
                    me->InterruptSpell(CURRENT_AUTOREPEAT_SPELL, true);
                if (me->HasUnitState(UNIT_STATE_MELEE_ATTACKING))
                {
                    // Keep the victim so the original AI resumes on it (AttackStart re-enables melee).
                    me->ClearUnitState(UNIT_STATE_MELEE_ATTACKING);
                    if (Unit* pVictim = me->GetVictim())
                        me->SendMeleeAttackStop(pVictim);
                }
                return true;
            }
            case BotActionType::Retreat:
            {
                // Every hit extends the enemy's leash, and the whole assisting group shares one
                // timer (combat scenario 11, 1.5): drop the target completely. HoldFire keeps the
                // victim and melee would resume; AttackStop clears it.
                if (me->GetCurrentSpell(CURRENT_AUTOREPEAT_SPELL))
                    me->InterruptSpell(CURRENT_AUTOREPEAT_SPELL, true);
                if (me->GetVictim())
                    me->AttackStop();

                // Pets count as attackers too. Same server path as the player's "Follow" button
                // (Unit::HandlePetCommand: AttackStop, interrupt, MoveFollow). Only re-sent when the
                // pet attacks again (defensive pets hit back) so it is not restarted every tick.
                if (Pet* pet = me->GetPet())
                    if (pet->IsAlive() && pet->GetCharmInfo() &&
                        (pet->GetVictim() || pet->GetCharmInfo()->GetCommandState() != COMMAND_FOLLOW))
                        pet->HandlePetCommand(COMMAND_FOLLOW, nullptr);

                if (action.holdMs && !me->HasUnitState(UNIT_STATE_ROOT))
                {
                    uint32 const now = WorldTimer::getMSTime();
                    // A new MovePoint every tick restarts the spline; only send it when the bot
                    // stopped or the previous hold ran out.
                    bool const onTheWay = me->IsMoving() && bot->Directives().IsMovementHeld(now);
                    if (!onTheWay)
                    {
                        RunTo(me, action.x, action.y, action.z);
                        bot->Directives().HoldMovement(now, action.holdMs);
                    }
                }
                // True even when rooted or standing at the point: the original rotation must not
                // run, it would pick the nearest attacker and hit it again.
                return true;
            }
            case BotActionType::UseObject:
            case BotActionType::Rest:
                break;      // handled above
            case BotActionType::PetAttack:
            case BotActionType::PetFollow:
            case BotActionType::PetAutocastOn:
            case BotActionType::PetAutocastOff:
                return ExecutePetAction(bot, action, pTarget);
        }

        return false;
    }

    bool Arbiter::ExecutePetAction(PartyBotAI* bot, BotAction const& action, Unit* pTarget)
    {
        Pet* pet = bot->me->GetPet();
        // A pet mind controlled by an enemy is not ours to command.
        if (!pet || !pet->IsAlive() || pet->IsCharmed() || !pet->GetCharmInfo())
            return false;
        CharmInfo* charmInfo = pet->GetCharmInfo();

        switch (action.type)
        {
            case BotActionType::PetAttack:
            {
                if (!pTarget || pet->GetVictim() == pTarget || !bot->IsValidHostileTarget(pTarget))
                    return false;
                // Same server path as the pet bar's Attack button (sets "commanded attack", so a
                // passive pet or one returning after Follow attacks too).
                pet->HandlePetCommand(COMMAND_ATTACK, pTarget);
                return pet->GetVictim() == pTarget;
            }
            case BotActionType::PetFollow:
            {
                // Same path as the Follow button (as in Retreat). Only re-sent when the pet attacks
                // or the rotation could send it in again: while it runs back after Follow,
                // PetAI::CanAttack refuses the rotation's AttackStart. Once it reached the bot the
                // flags are cleared and Follow is sent once more.
                // An idle pet already on Follow is not sent it again: each resend runs AttackStop
                // and InterruptNonMeleeSpells on it (cuts channels such as Consume Shadows).
                if (!pet->GetVictim() && charmInfo->HasCommandState(COMMAND_FOLLOW))
                    return true;
                if (pet->GetVictim() || !(charmInfo->IsReturning() && charmInfo->IsCommandFollow()))
                    pet->HandlePetCommand(COMMAND_FOLLOW, nullptr);
                return true;
            }
            case BotActionType::PetAutocastOn:
            case BotActionType::PetAutocastOff:
            {
                if (!action.spell || !pet->HasSpell(action.spell->Id) || !action.spell->IsAutocastable())
                    return false;
                // Same calls as the client's autocast toggle (WorldSession::HandlePetSpellAutocastOpcode).
                bool const on = action.type == BotActionType::PetAutocastOn;
                pet->ToggleAutocast(action.spell->Id, on);
                charmInfo->SetSpellAutocast(action.spell->Id, on);
                return true;
            }
            default:
                return false;
        }
    }

    bool Arbiter::ExecuteUseObject(PartyBotAI* bot, BotAction const& action)
    {
        Player* me = bot->me;
        if (action.target.IsEmpty() || !action.target.IsGameObject())
            return false;
        GameObject* go = me->GetMap()->GetGameObject(action.target);
        if (!go || !go->IsInWorld() || !go->isSpawned())
            return false;

        // In reach: same check and call as HandlePartyBotUseGObjectHelper (`.partybot usegobject`).
        // GameObject::Use runs the script hook (OnGameObjectUse) first, then the type handler.
        if (me->IsWithinDist(go, INTERACTION_DISTANCE))
        {
            if (me->IsNonMeleeSpellCasted(false, false, true))
                return false;   // let the cast finish; the next tick uses it
            go->Use(me);
            return true;
        }

        if (me->HasUnitState(UNIT_STATE_ROOT))
            return false;

        // Walk to a point USE_APPROACH_YARDS in front of the object (on the side the bot comes
        // from): the object's own position can be inside the wall it is mounted on.
        uint32 const now = WorldTimer::getMSTime();
        bool const onTheWay = me->IsMoving() && bot->Directives().IsMovementHeld(now);
        if (!onTheWay)
        {
            float const angle = go->GetAngle(me);
            float const x = go->GetPositionX() + USE_APPROACH_YARDS * std::cos(angle);
            float const y = go->GetPositionY() + USE_APPROACH_YARDS * std::sin(angle);
            float const z = go->GetPositionZ();
            RunTo(me, x, y, z);
        }
        // Keep the original follow / chase off while walking. Renewed every tick the module
        // proposes it, so it runs out about holdMs after the module stops.
        uint32 const holdMs = action.holdMs ? action.holdMs : MOVEMENT_HOLD_MS;
        bot->Directives().HoldMovement(now, holdMs);
        return true;
    }
}
