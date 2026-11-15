#include "Commander/Behaviors/CurrentAction.h"
#include "Commander/Behaviors/IBehavior.h"
#include "Commander/BotDirectives.h"
#include "Commander/CommanderMgr.h"
#include "Commander/Perception/Values/ValueModel.h"
#include "Commander/Perception/WorldModel.h"
#include "PartyBotAI.h"
#include "Spell.h"
#include "SpellEntry.h"
#include "Timer.h"
#include <algorithm>
#include <cstring>

namespace Commander
{
namespace CurrentAction
{
    namespace
    {
        bool WalkAction(BotActionType type)
        {
            switch (type)
            {
                case BotActionType::MoveTo:
                case BotActionType::Backpedal:
                case BotActionType::Chase:
                case BotActionType::Retreat:
                case BotActionType::UseObject:
                    return true;
                default:
                    return false;
            }
        }

        bool MovementEnds(SpellEntry const* spell, bool channel)
        {
            return channel ? spell->HasChannelInterruptFlag(AURA_INTERRUPT_MOVING_CANCELS)
                           : spell->HasSpellInterruptFlag(SPELL_INTERRUPT_FLAG_MOVEMENT);
        }
    }

    bool NeedsStandingStill(PartyBotAI const* bot, SpellEntry const* spell)
    {
        if (!spell)
            return false;
        if (spell->IsChanneledSpell())
            return MovementEnds(spell, true);
        return spell->GetCastTime(bot->me) > 0 && MovementEnds(spell, false);
    }

    State Read(PartyBotAI const* bot, WorldModel const& world, TeamPlan const& plan)
    {
        State s;
        Player* me = bot->me;
        PartyMemberFacts const* self = world.FindMember(me->GetObjectGuid());

        Spell const* spell = me->GetCurrentSpell(CURRENT_GENERIC_SPELL);
        if (spell && spell->getState() == SPELL_STATE_PREPARING && spell->GetCastTime() > 0)
            s.channel = false;
        else
        {
            spell = me->GetCurrentSpell(CURRENT_CHANNELED_SPELL);
            if (!spell || spell->getState() != SPELL_STATE_CASTING)
                spell = nullptr;
            s.channel = spell != nullptr;
        }
        if (spell && spell->m_spellInfo && spell->GetCastedTime() > 0)
        {
            s.spell = spell->m_spellInfo;
            s.leftMs = spell->GetCastedTime();      // counts down to 0
            s.movementEnds = MovementEnds(s.spell, s.channel);

            // 1. The module that values its own spell live.
            float best = -1.0f;
            for (auto const& slot : sCommanderMgr.GetBehaviors())
            {
                if (!slot->enabled.load(std::memory_order_relaxed) || !slot->module->AppliesTo(bot))
                    continue;
                char const* reason = "";
                float const v = slot->module->ContinueValue(bot, world, plan, s.spell, reason);
                if (v > best)
                {
                    best = v;
                    s.keepFrom = slot->module->GetId();
                    s.keepReason = reason;
                }
            }
            if (best >= 0.0f)
                s.keep = best;
            else
            {
                // 2. The arbiter's order it was cast for.
                ExecutedAction const& last = bot->Directives().GetLastExecuted();
                if (last.type == BotActionType::Cast && last.spell && last.spell->Id == s.spell->Id && last.valued)
                {
                    float share = 1.0f;
                    if (s.channel)
                    {
                        int32 const duration = s.spell->GetDuration();
                        share = duration > 0 ? std::min(1.0f, float(s.leftMs) / float(duration)) : 1.0f;
                    }
                    s.keep = std::max(0.0f, last.net) * share;
                    s.keepFrom = last.source;
                    s.keepReason = s.channel ? "the rest of the order's channel" : "the order's cast";
                }
                // 3. A rotation cast: the time spent (a cast starts again from zero), a channel
                // one global cooldown (the ticks that landed are kept).
                else if (self)
                {
                    float lostSec = Value::GCD_SEC;
                    if (!s.channel)
                    {
                        int32 const total = spell->GetCastTime();
                        lostSec = total > int32(s.leftMs) ? float(total - int32(s.leftMs)) / 1000.0f : 0.0f;
                    }
                    s.keep = Value::LostAction(world, *self, lostSec);
                    s.keepFrom = "rotation";
                    s.keepReason = s.channel ? "a global cooldown to start again" : "the cast time spent";
                }
            }
        }

        // A module's walk under way.
        BotDirectives const& directives = bot->Directives();
        uint32 const now = WorldTimer::getMSTime();
        if (char const* owner = directives.GetMovementOwner(now))
        {
            ExecutedAction const& walk = directives.GetLastWalk();
            if (me->IsMoving() && walk.valued && walk.source && !strcmp(walk.source, owner))
            {
                s.walkOwner = owner;
                s.walkKeep = std::max(0.0f, walk.net);
            }
        }
        return s;
    }

    float InterruptCost(PartyBotAI const* bot, State const& state, BotAction const& action, char const*& what)
    {
        what = "";
        float cost = 0.0f;
        if (state.Casting())
        {
            // An item's spell ends a running cast too (SpellCaster::SetCurrentCastedSpell).
            bool const ends = action.type == BotActionType::Cast || action.type == BotActionType::UseItem ||
                              action.type == BotActionType::Rest ||
                              (state.movementEnds && WalkAction(action.type));
            if (ends && state.keep > 0.0f)
            {
                cost += state.keep;
                what = state.channel ? "ends the channel" : "ends the cast";
            }
        }
        if (state.walkKeep > 0.0f && action.type == BotActionType::Cast && NeedsStandingStill(bot, action.spell))
        {
            cost += state.walkKeep;
            what = *what ? "ends the cast and the walk" : "stops the walk";
        }
        return cost;
    }
}
}
