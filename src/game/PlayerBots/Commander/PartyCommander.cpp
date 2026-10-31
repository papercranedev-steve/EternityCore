#include "Commander/PartyCommander.h"
#include "Commander/Behaviors/Arbiter.h"
#include "PartyBotAI.h"
#include "Timer.h"

namespace Commander
{
    void PartyCommander::Update(PartyBotAI* bot, uint32 nowMs, uint32 perceptionMs)
    {
        if (m_world.version && WorldTimer::getMSTimeDiff(m_world.refreshedAtMs, nowMs) < perceptionMs)
            return;

        Refresh(bot, nowMs);
    }

    void PartyCommander::Refresh(PartyBotAI* /*bot*/, uint32 nowMs)
    {
        // Step 2: fill enemies / threat / packs from the bot's map here.
        ++m_world.version;
        m_world.refreshedAtMs = nowMs;
    }

    bool PartyCommander::RunCombat(PartyBotAI* bot, BehaviorList const& behaviors)
    {
        return Arbiter::Run(bot, m_world, m_plan, behaviors);
    }
}
