/*
 * PartyBot Commander: one instance per (group, map instance).
 * Keyed by map so that it is only ever touched from that map's update thread.
 */

#ifndef MANGOS_COMMANDER_PARTY_COMMANDER_H
#define MANGOS_COMMANDER_PARTY_COMMANDER_H

#include "Commander/Perception/WorldModel.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/ThreatHistory.h"
#include "Commander/Registry.h"

class PartyBotAI;

namespace Commander
{
    class PartyCommander
    {
    public:
        PartyCommander(uint32 groupId, uint32 mapId, uint32 instanceId)
            : m_groupId(groupId), m_mapId(mapId), m_instanceId(instanceId) {}

        // Every bot tick, in and out of combat. Refreshes facts at most once per perceptionMs,
        // so several bots of the same party share one refresh.
        void Update(PartyBotAI* bot, uint32 nowMs, uint32 perceptionMs);

        // Combat tick of one bot: behaviors + arbiter. True = action taken, skip original AI.
        bool RunCombat(PartyBotAI* bot, BehaviorList const& behaviors);

        WorldModel const& GetWorld() const { return m_world; }
        TeamPlan const& GetPlan() const { return m_plan; }
        uint32 GetGroupId() const { return m_groupId; }
        uint32 GetMapId() const { return m_mapId; }
        uint32 GetInstanceId() const { return m_instanceId; }

    private:
        void Refresh(PartyBotAI* bot, uint32 nowMs);

        uint32 m_groupId;
        uint32 m_mapId;
        uint32 m_instanceId;
        WorldModel m_world;
        TeamPlan m_plan;
        ThreatHistory m_threatHistory;
    };
}

#endif
