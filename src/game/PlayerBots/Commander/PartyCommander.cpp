#include "Commander/PartyCommander.h"
#include "Commander/Behaviors/Arbiter.h"
#include "Commander/Perception/WorldModelBuilder.h"
#include "Commander/Coordination/ClearPlanner.h"
#include "PartyBotAI.h"
#include "Chat.h"
#include "Timer.h"

namespace Commander
{
    namespace
    {
        // Pack scans use a grid search; packs move slowly, once a second is enough.
        uint32 const PACK_REFRESH_MS = 1000;
    }

    void PartyCommander::Update(PartyBotAI* bot, uint32 nowMs, uint32 perceptionMs, AssignerList const& assigners)
    {
        if (m_world.version && WorldTimer::getMSTimeDiff(m_world.refreshedAtMs, nowMs) < perceptionMs)
            return;

        Refresh(bot, nowMs);
        RunAssigners(assigners);
        ClearPlanner::Step(bot, *this, nowMs);  // no-op unless `.partybot ai clear` is running
        FlushNotes(bot);
    }

    void PartyCommander::FlushNotes(PartyBotAI* bot)
    {
        std::vector<std::string>& notes = m_plan.notes;
        if (notes.empty())
            return;
        Player* leader = bot->GetPartyLeader();
        if (leader && leader->IsInWorld() && leader->GetMap() == bot->me->GetMap())
        {
            ChatHandler handler(leader);
            for (std::string const& note : notes)
                handler.PSendSysMessage("[PartyBot] %s", note.c_str());
        }
        notes.clear();
    }

    void PartyCommander::RunAssigners(AssignerList const& assigners)
    {
        for (auto const& slot : assigners)
        {
            if (slot->enabled.load(std::memory_order_relaxed))
                slot->module->Assign(m_world, m_plan);
            else
                slot->module->Clear(m_plan);
        }
        ++m_plan.version;
    }

    void PartyCommander::Refresh(PartyBotAI* bot, uint32 nowMs)
    {
        WorldModel previous;
        previous.enemies = std::move(m_world.enemies);
        WorldModelBuilder::RefreshCombat(bot, m_world);
        m_threatHistory.Annotate(m_world, previous, nowMs);
        m_combatRates.Annotate(m_world, nowMs);

        if (m_world.version == 0 || WorldTimer::getMSTimeDiff(m_world.packsRefreshedAtMs, nowMs) >= PACK_REFRESH_MS)
        {
            WorldModelBuilder::RefreshPacks(bot, m_world);
            m_world.packsRefreshedAtMs = nowMs;
        }

        ++m_world.version;
        m_world.refreshedAtMs = nowMs;
    }

    bool PartyCommander::RunCombat(PartyBotAI* bot, BehaviorList const& behaviors)
    {
        return Arbiter::Run(bot, m_world, m_plan, behaviors);
    }
}
