#include "Commander/Behaviors/Arbiter.h"
#include "Commander/BotDirectives.h"
#include "Commander/CommanderMgr.h"
#include "PartyBotAI.h"
#include "Map.h"
#include "MotionMaster.h"
#include "Pet.h"
#include "SpellEntry.h"
#include "Timer.h"
#include <algorithm>
#include <cstdio>

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
                case BotActionType::Hold:   return "hold";
                case BotActionType::HoldFire: return "holdfire";
                case BotActionType::Retreat: return "retreat";
            }
            return "?";
        }

        char const* VetoName(VetoType type)
        {
            switch (type)
            {
                case VetoType::None:   return "none";
                case VetoType::Dispel: return "no-dispel";
                case VetoType::Interrupt: return "no-interrupt";
                case VetoType::Heal:   return "no-heal";
            }
            return "?";
        }

        std::string TargetName(PartyBotAI const* bot, ObjectGuid guid)
        {
            if (guid.IsEmpty())
                return "self";
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
        snprintf(buf, sizeof(buf), "%s %s spell%u", ActionName(p.action.type),
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

        // Keeps the original rotation's own taunt switched off while a module decides taunts.
        // Expires by itself if the module is disabled or the Commander is turned off.
        for (IBehavior const* b : active)
        {
            if (b->ControlsTaunt(bot))
            {
                directives.ControlTaunt(now, 3 * sCommanderMgr.GetBotUpdateInterval());
                break;
            }
        }

        std::vector<Proposal> proposals;
        for (IBehavior const* b : active)
            b->Propose(bot, world, plan, proposals);

        if (proposals.empty())
            return false;

        for (IBehavior const* b : active)
            b->Adjust(bot, world, proposals);

        DecisionRecord record;
        record.timeMs = now;

        // Value-scored proposals that are not worth it, and vetoes.
        std::vector<Proposal> replace;
        uint32 const vetoMs = VETO_TICKS * sCommanderMgr.GetBotUpdateInterval();
        for (Proposal const& p : proposals)
        {
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
            replace.push_back(p);
        }

        std::stable_sort(replace.begin(), replace.end(), [](Proposal const& a, Proposal const& b)
        {
            if (a.tier != b.tier)
                return a.tier < b.tier;
            return a.score > b.score;
        });

        bool executed = false;
        bool rotationMayRun = false;
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

                if (Execute(bot, p))
                {
                    executed = true;
                    // A target switch only changes the victim: the original rotation can act on
                    // the new target in the same tick. Otherwise a tank switching between loose
                    // adds every tick never casts Thunder Clap or Sunder (combat scenario 9).
                    rotationMayRun = p.action.type == BotActionType::Attack;
                    r.result = CandidateRecord::Result::Executed;
                    directives.SetLastDecision(now, std::string(p.source) + " " + r.what);
                }
                else
                    r.result = CandidateRecord::Result::Failed;
            }
            if (!executed)
                directives.SetLastDecision(now, "failed: " + std::string(replace.front().source) + " " + Describe(bot, replace.front()));
        }

        directives.RecordDecision(std::move(record));
        return executed && !rotationMayRun;
    }

    bool Arbiter::Execute(PartyBotAI* bot, Proposal const& proposal)
    {
        Player* me = bot->me;
        BotAction const& action = proposal.action;

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
                Unit* pCastTarget = pTarget ? pTarget : me;
                if (!bot->CanTryToCastSpell(pCastTarget, action.spell))
                    return false;
                return bot->DoCastSpell(pCastTarget, action.spell) == SPELL_CAST_OK;
            }
            case BotActionType::Attack:
            {
                if (!pTarget || me->GetVictim() == pTarget || !bot->IsValidHostileTarget(pTarget))
                    return false;
                if (me->IsMounted())    // Unit::Attack refuses while mounted
                    me->RemoveSpellsCausingAura(SPELL_AURA_MOUNTED);
                return bot->AttackStart(pTarget);
            }
            case BotActionType::MoveTo:
            {
                if (me->HasUnitState(UNIT_STATE_ROOT))
                    return false;
                me->GetMotionMaster()->MovePoint(0, action.x, action.y, action.z, MOVE_PATHFINDING);
                bot->Directives().HoldMovement(WorldTimer::getMSTime(), action.holdMs);
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
                        me->GetMotionMaster()->MovePoint(0, action.x, action.y, action.z, MOVE_PATHFINDING);
                        bot->Directives().HoldMovement(now, action.holdMs);
                    }
                }
                // True even when rooted or standing at the point: the original rotation must not
                // run, it would pick the nearest attacker and hit it again.
                return true;
            }
        }

        return false;
    }
}
