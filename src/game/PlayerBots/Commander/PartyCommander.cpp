#include "Commander/PartyCommander.h"
#include "Commander/Behaviors/Arbiter.h"
#include "Commander/Perception/WorldModelBuilder.h"
#include "Commander/Coordination/ClearPlanner.h"
#include "Commander/Encounters/EncounterRegistry.h"
#include "Commander/Encounters/EncounterBehavior.h"
#include "Commander/CommanderMgr.h"
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

    PartyCommander::PartyCommander(uint32 groupId, uint32 mapId, uint32 instanceId)
        : m_groupId(groupId), m_mapId(mapId), m_instanceId(instanceId)
    {
        EncounterRegistry::Create(mapId, m_encounters);
    }

    void PartyCommander::Update(PartyBotAI* bot, uint32 nowMs, uint32 perceptionMs, AssignerList const& assigners)
    {
        if (m_world.version && WorldTimer::getMSTimeDiff(m_world.refreshedAtMs, nowMs) < perceptionMs)
            return;

        Refresh(bot, nowMs);
        UpdateEncounter(bot);
        RunAssigners(assigners);
        if (m_activeEncounter)
            m_activeEncounter->AdjustPlan(m_world, m_plan);
        ClearPlanner::Step(bot, *this, nowMs);  // no-op unless `.partybot ai clear` is running
        FlushNotes(bot);
    }

    void PartyCommander::UpdateEncounter(PartyBotAI* bot)
    {
        if (m_encounters.empty())
            return;     // no boss module on this map: nothing else runs

        bool const enabled = sCommanderMgr.IsModuleEnabled(EncounterBehavior::Id());
        if (m_activeEncounter && (!enabled || !m_activeEncounter->Matches(m_world, m_mapId)))
        {
            m_activeEncounter->OnReset();
            m_activeEncounter = nullptr;
        }
        if (!enabled)
            return;

        if (!m_activeEncounter)
        {
            for (auto const& encounter : m_encounters)
            {
                if (encounter->Matches(m_world, m_mapId))
                {
                    m_activeEncounter = encounter.get();
                    break;
                }
            }
        }
        if (!m_activeEncounter)
            return;

        m_activeEncounter->Refresh(bot, m_world);
        m_activeEncounter->AddFacts(m_world);
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
