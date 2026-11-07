#include "Commander/Behaviors/CastBreak.h"
#include "Commander/BotDirectives.h"
#include "Commander/CommanderMgr.h"
#include "Commander/PartyCommander.h"
#include "Commander/Perception/ValueModel.h"
#include "Commander/Perception/WorldModel.h"
#include "PartyBotAI.h"
#include "Spell.h"
#include "SpellEntry.h"
#include "Timer.h"
#include <algorithm>
#include <cstdio>

namespace Commander
{
namespace CastBreak
{
    bool Update(PartyBotAI* bot, CastBreakState& state)
    {
        Player* me = bot->me;
        CurrentSpellTypes type = CURRENT_GENERIC_SPELL;
        Spell* spell = me->GetCurrentSpell(CURRENT_GENERIC_SPELL);
        if (!spell || spell->getState() != SPELL_STATE_PREPARING)
        {
            type = CURRENT_CHANNELED_SPELL;
            spell = me->GetCurrentSpell(CURRENT_CHANNELED_SPELL);
            if (!spell || spell->getState() != SPELL_STATE_CASTING)
                return false;
        }

        uint32 const now = WorldTimer::getMSTime();
        if (WorldTimer::getMSTimeDiff(state.lastCheckMs, now) < CHECK_INTERVAL_MS)
            return false;
        state.lastCheckMs = now;
        if (state.lastBreakMs && WorldTimer::getMSTimeDiff(state.lastBreakMs, now) < COOLDOWN_MS)
            return false;

        if (!sCommanderMgr.IsEnabled() || !me->IsInCombat() || bot->IsInDuel())
            return false;
        // Same map as the bot, so the commander belongs to this map thread.
        std::shared_ptr<PartyCommander> commander = sCommanderMgr.FindCommander(me);
        if (!commander)
            return false;
        WorldModel const& world = commander->GetWorld();
        TeamPlan const& plan = commander->GetPlan();
        PartyMemberFacts const* self = world.FindMember(me->GetObjectGuid());
        if (!self)
            return false;

        bool const channel = type == CURRENT_CHANNELED_SPELL;
        uint32 const leftMs = spell->GetCastedTime();     // m_timer counts down to 0
        if (!leftMs)
            return false;

        float best = 0.0f;
        char const* bestSource = "";
        char const* bestReason = "";
        for (auto const& slot : sCommanderMgr.GetBehaviors())
        {
            if (!slot->enabled.load(std::memory_order_relaxed) || !slot->module->AppliesTo(bot))
                continue;
            char const* reason = "";
            float const benefit = slot->module->CastBreakBenefit(bot, world, plan, leftMs, reason);
            if (benefit > best)
            {
                best = benefit;
                bestSource = slot->module->GetId();
                bestReason = reason;
            }
        }
        if (best <= 0.0f)
            return false;

        float lostSec = Value::GCD_SEC;
        if (!channel)
        {
            int32 const total = spell->GetCastTime();
            lostSec = total > int32(leftMs) ? float(total - int32(leftMs)) / 1000.0f : 0.0f;
        }
        float const cost = Value::LostAction(world, *self, lostSec);
        if (best <= cost)
            return false;

        uint32 const spellId = spell->m_spellInfo->Id;
        me->InterruptSpell(type, true);
        state.lastBreakMs = now;

        char what[64];
        snprintf(what, sizeof(what), "cast-break spell%u", spellId);
        CandidateRecord r;
        r.source = bestSource;
        r.what = what;
        r.tier = uint8(ProposalTier::Urgent);
        r.valued = true;
        r.benefit = best;
        r.cost = cost;
        r.score = best - cost;
        r.reason = bestReason;
        r.result = CandidateRecord::Result::Executed;
        DecisionRecord record;
        record.timeMs = now;
        record.candidates.push_back(std::move(r));
        BotDirectives& directives = bot->Directives();
        directives.SetLastDecision(now, std::string(bestSource) + " " + what);
        directives.RecordDecision(std::move(record));
        return true;
    }
}
}
