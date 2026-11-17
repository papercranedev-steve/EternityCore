#include "Commander/PartyCommander.h"
#include "Commander/Behaviors/Arbiter.h"
#include "Commander/Perception/WorldModelBuilder.h"
#include "Commander/Coordination/Situation/ClearPlanner.h"
#include "Commander/Coordination/Situation/RetreatPlanner.h"
#include "Commander/Coordination/Situation/Patrol/PatrolPlanner.h"
#include "Commander/Coordination/Situation/Los/CasterLurePlanner.h"
#include "Commander/Encounters/EncounterRegistry.h"
#include "Commander/Encounters/EncounterBehavior.h"
#include "Commander/CommanderMgr.h"
#include "Commander/BotDirectives.h"
#include "Commander/Commands/BotSpeech.h"
#include "Commander/Perception/Tanks.h"
#include "PartyBotAI.h"
#include "Group.h"
#include "Chat.h"
#include "Map.h"
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

    void PartyCommander::Update(PartyBotAI* bot, uint32 nowMs, uint32 perceptionMs, AssignerList const& assigners,
                                BehaviorList const& behaviors)
    {
        if (m_world.version && WorldTimer::getMSTimeDiff(m_world.refreshedAtMs, nowMs) < perceptionMs)
            return;

        Trace::Use trace(&m_trace, nowMs);
        Trace::Scope scope("brain");
        scope.Label("refresh");
        Refresh(bot, nowMs);
        UpdateEncounter(bot);
        m_plan.noRest = m_activeEncounter && m_activeEncounter->ForbidsRest();
        RunAssigners(assigners);
        if (m_activeEncounter)
            m_activeEncounter->AdjustPlan(m_world, m_plan);
        PatrolPlanner::Step(m_plan, m_world, bot->me->GetMap(), nowMs);    // no-op unless guarding off a patrol
        CasterLurePlanner::Step(m_plan, m_world, bot->me->GetMap(), nowMs);    // no-op without a lure searching
        ApplyEmergencyRoles(bot);
        ApplyRoles(bot);
        RetreatPlanner::Step(bot, *this);       // a retreat the player's run asked for, door steps
        ReleaseMovementAfterRetreat(bot);
        ClearPlanner::Step(bot, *this, nowMs);  // no-op unless `.partybot ai clear` is running
        FlushNotes(bot);
        m_threatFeed.Push(bot, m_world, nowMs);
        WakeBots(bot, behaviors);
    }

    void PartyCommander::WakeBots(PartyBotAI* bot, BehaviorList const& behaviors)
    {
        if (!m_world.HasPartyEnemy())
            return;
        Group* group = bot->me->GetGroup();
        if (!group)
            return;
        // Same map only: other maps belong to other threads.
        for (GroupReference* itr = group->GetFirstMember(); itr; itr = itr->next())
        {
            Player* member = itr->getSource();
            if (!member || member == bot->me || !member->IsInWorld() || member->GetMap() != bot->me->GetMap() ||
                !member->IsAlive() || !member->AI())
                continue;
            PartyBotAI* ai = dynamic_cast<PartyBotAI*>(member->AI());
            if (!ai)
                continue;
            for (auto const& slot : behaviors)
            {
                if (slot->enabled.load(std::memory_order_relaxed) && slot->module->AppliesTo(ai) &&
                    slot->module->WantsTickNow(ai, m_world, m_plan))
                {
                    ai->WakeUp();
                    break;
                }
            }
        }
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

    void PartyCommander::ApplyEmergencyRoles(PartyBotAI* bot)
    {
        Group* group = bot->me->GetGroup();
        if (!group)
            return;
        bool const fighting = m_world.HasPartyEnemy();
        for (GroupReference* itr = group->GetFirstMember(); itr; itr = itr->next())
        {
            Player* member = itr->getSource();
            if (!member || !member->IsInWorld() || member->GetMap() != bot->me->GetMap() || !member->AI())
                continue;
            PartyBotAI* ai = dynamic_cast<PartyBotAI*>(member->AI());
            if (!ai)
                continue;
            EmergencyRole const* given = nullptr;
            for (EmergencyRole const& r : m_plan.emergencyRoles)
                if (r.bot == member->GetObjectGuid())
                    given = &r;
            if (given)
            {
                if (uint8(ai->m_role) == given->role)
                    continue;
                CombatBotRoles const before = ai->m_role;
                ai->SetEmergencyRole(CombatBotRoles(given->role));
                m_plan.notes.push_back(std::string(member->GetName()) + ": " + RoleText(before) + " -> " +
                    RoleText(ai->m_role) + (given->role == ROLE_TANK
                        ? std::string(" for this fight, takes over as ") + StandIn::Describe(given->way) + "."
                        : std::string(" for this fight.")));
            }
            else if (ai->HasEmergencyRole() && !fighting)
            {
                CombatBotRoles const before = ai->m_role;
                ai->RestoreRole();
                if (ai->m_role != before)
                    m_plan.notes.push_back(std::string(member->GetName()) + ": back to " + RoleText(ai->m_role) + ".");
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
        // Said by the main tank bot (it leads the pulls), else by the bot running this refresh.
        Player* speaker = bot->me;
        if (PartyMemberFacts const* tank = Tanks::Main(m_world, true))
            if (Player* p = bot->me->GetMap()->GetPlayer(tank->guid))
                speaker = p;
        Player* leader = bot->GetPartyLeader();
        for (std::string const& note : notes)
            BotSpeech::Report(speaker, leader, note);
        uint32 const now = WorldTimer::getMSTime();
        for (std::string& note : notes)
        {
            NoteRecord record;
            record.timeMs = now;
            record.text = std::move(note);
            m_noteLog.push_back(std::move(record));
            if (m_noteLog.size() > NOTE_LOG_SIZE)
                m_noteLog.pop_front();
        }
        notes.clear();
    }

    void PartyCommander::RunAssigners(AssignerList const& assigners)
    {
        // 1. Situation, then the task modules that still assign directly (IAssigner.h).
        for (AssignStage stage : { AssignStage::Situation, AssignStage::Tasks })
        {
            for (auto const& slot : assigners)
            {
                if (slot->module->GetStage() != stage)
                    continue;
                Trace::Scope scope("assigner");
                scope.Label("%s", slot->module->GetId());
                if (slot->enabled.load(std::memory_order_relaxed))
                    slot->module->Assign(m_world, m_plan);
                else
                    slot->module->Clear(m_plan);
            }
        }

        // 2. Duties: every module offers against the same plan, one allocation answers them all.
        std::vector<DutyNeed> needs;
        std::vector<DutyReserve> reserves;
        for (auto const& slot : assigners)
        {
            if (slot->module->GetStage() != AssignStage::Duties)
                continue;
            Trace::Scope scope("assigner");
            scope.Label("%s offer", slot->module->GetId());
            if (slot->enabled.load(std::memory_order_relaxed))
            {
                slot->module->Offer(m_world, m_plan, needs);
                slot->module->OfferReserves(m_world, m_plan, reserves);
            }
            else
                slot->module->Clear(m_plan);
        }
        m_plan.offers = needs;      // the log of what was offered (TeamPlan::offers)
        m_plan.reserves = reserves;
        m_plan.duties = DutyAllocator::Allocate(std::move(needs), m_plan.reserves);
        for (auto const& slot : assigners)
            if (slot->module->GetStage() == AssignStage::Duties && slot->enabled.load(std::memory_order_relaxed))
            {
                Trace::Scope scope("assigner");
                scope.Label("%s apply", slot->module->GetId());
                slot->module->Apply(m_world, m_plan.duties, m_plan);
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
        m_leaderRun.Annotate(m_world, nowMs);

        if (m_world.version == 0 || WorldTimer::getMSTimeDiff(m_world.packsRefreshedAtMs, nowMs) >= PACK_REFRESH_MS)
        {
            WorldModelBuilder::RefreshPacks(bot, m_world);
            m_world.packsRefreshedAtMs = nowMs;
        }
        // (after the packs: an enemy's pack mates are left out of its route check)
        m_meleeRoutes.Annotate(bot->me->GetMap(), m_world, nowMs);
        // (after the rates: time to the flee line uses the party's damage)
        m_fleePredictor.Annotate(bot->me->GetMap(), m_world, nowMs);
        // (the pull of the last refresh: planned on the map thread before this, PullPlanner)
        m_fleePredictor.AnnotatePull(bot->me->GetMap(), m_world, m_plan);

        ++m_world.version;
        m_world.refreshedAtMs = nowMs;
        // (after the version step, so the field is tagged with this refresh. m_plan is the
        // previous refresh's: the pull's planned fight spot, the assigners run after this.)
        m_positionField.Build(bot->me->GetMap(), m_world, m_plan, nowMs);
    }

    bool PartyCommander::RunCombat(PartyBotAI* bot, BehaviorList const& behaviors)
    {
        Trace::Use trace(&m_trace, WorldTimer::getMSTime());
        Trace::Scope scope("bot");
        scope.Label("%s", bot->me->GetName());
        return Arbiter::Run(bot, m_world, m_plan, behaviors);
    }
}
