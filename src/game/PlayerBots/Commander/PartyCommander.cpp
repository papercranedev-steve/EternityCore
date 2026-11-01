#include "Commander/PartyCommander.h"
#include "Commander/Behaviors/Arbiter.h"
#include "Commander/Perception/WorldModelBuilder.h"
#include "Commander/Coordination/ClearPlanner.h"
#include "Commander/Encounters/EncounterRegistry.h"
#include "Commander/Encounters/EncounterBehavior.h"
#include "Commander/CommanderMgr.h"
#include "Commander/BotDirectives.h"
#include "PartyBotAI.h"
#include "Group.h"
#include "Chat.h"
#include "Timer.h"

namespace Commander
{
    namespace
    {
        // Pack scans use a grid search; packs move slowly, once a second is enough.
        uint32 const PACK_REFRESH_MS = 1000;

        char const* RoleText(CombatBotRoles role)
        {
            switch (role)
            {
                case ROLE_TANK:      return "tank";
                case ROLE_HEALER:    return "healer";
                case ROLE_MELEE_DPS: return "melee damage";
                case ROLE_RANGE_DPS: return "ranged damage";
                default:             return "auto";
            }
        }
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
        ApplyRoles(bot);
        ReleaseMovementAfterRetreat(bot);
        ClearPlanner::Step(bot, *this, nowMs);  // no-op unless `.partybot ai clear` is running
        FlushNotes(bot);
    }

    void PartyCommander::ReleaseMovementAfterRetreat(PartyBotAI* bot)
    {
        bool const retreating = m_plan.retreat.active;
        bool const ended = m_wasRetreating && !retreating;
        m_wasRetreating = retreating;
        if (!ended)
            return;

        // RetreatAssigner is pure and cannot reach the bots when it ends the retreat (done,
        // timeout, module off). Without this the last Retreat MoveTo hold (path length + 1 s)
        // keeps every bot walking to the point after the fight is over. RetreatPlanner::Stop
        // already released them; doing it again is harmless. Same map only: other maps belong
        // to other threads.
        Group* group = bot->me->GetGroup();
        if (!group)
            return;
        for (GroupReference* itr = group->GetFirstMember(); itr; itr = itr->next())
        {
            Player* member = itr->getSource();
            if (!member || !member->IsInWorld() || member->GetMap() != bot->me->GetMap() || !member->AI())
                continue;
            if (PartyBotAI* ai = dynamic_cast<PartyBotAI*>(member->AI()))
                ai->Directives().ReleaseMovement();
        }
    }

    void PartyCommander::ApplyRoles(PartyBotAI* bot)
    {
        // RoleAssigner is pure and only fills TeamPlan::roles; the bots of this map are changed
        // here (map thread). It keeps the old result during fights; a bot fighting on its own
        // (the assigner cannot see that) keeps its role until it is out of combat too.
        if (m_plan.roles.empty() || m_world.HasPartyEnemy())
            return;
        Group* group = bot->me->GetGroup();
        if (!group)
            return;
        for (GroupReference* itr = group->GetFirstMember(); itr; itr = itr->next())
        {
            Player* member = itr->getSource();
            if (!member || !member->IsInWorld() || member->GetMap() != bot->me->GetMap() || !member->AI() ||
                member->IsInCombat())
                continue;
            PartyBotAI* ai = dynamic_cast<PartyBotAI*>(member->AI());
            if (!ai || ai->IsRoleLocked())
                continue;
            for (RoleDuty const& d : m_plan.roles)
            {
                if (d.bot != member->GetObjectGuid() || d.role == uint8(ai->m_role))
                    continue;
                CombatBotRoles const before = ai->m_role;
                ai->AssignRole(CombatBotRoles(d.role));
                if (ai->m_role != before)
                    m_plan.notes.push_back(std::string(member->GetName()) + ": " + RoleText(before) + " -> " +
                                           RoleText(ai->m_role) + " (" + d.reason + ")");
                break;
            }
        }
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
