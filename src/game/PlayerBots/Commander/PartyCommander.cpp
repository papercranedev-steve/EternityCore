#include "Commander/PartyCommander.h"
#include "Commander/Behaviors/Arbiter.h"
#include "Commander/Perception/WorldModelBuilder.h"
#include "PartyBotAI.h"
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
