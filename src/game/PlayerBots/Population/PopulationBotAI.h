/*
 * World population bot: a temporary character (never saved) that only adds atmosphere.
 * It refuses group invites, trades and duels. Phase 1a behaviour is idle (emotes, sitting,
 * turning); city and field behaviours come in phases 1b / 1c.
 *
 * Threads: UpdateAI and OnPacketReceived run on the map thread. Settings come from
 * PopulationMgr atomics; nothing else of the manager is touched here.
 */

#ifndef MANGOS_POPULATION_BOT_AI_H
#define MANGOS_POPULATION_BOT_AI_H

#include "CombatBotBaseAI.h"
#include "Population/PopulationTypes.h"
#include "Timer.h"
#include <memory>

namespace Population
{
    class PopulationBotAI : public CombatBotBaseAI
    {
    public:
        PopulationBotAI(SpawnRequest const& request, std::string name, std::shared_ptr<BotHandle> handle);
        ~PopulationBotAI() override;

        bool OnSessionLoaded(PlayerBotEntry* entry, WorldSession* sess) override;
        void OnPlayerLogin() override;
        void UpdateAI(uint32 const diff) override;
        void OnPacketReceived(WorldPacket const* packet) override;

        SpawnRequest const& GetRequest() const { return m_request; }
        std::string const& GetBotName() const { return m_name; }
        bool IsInitialized() const { return m_initialized; }

        // Combat comes with the field behaviour (phase 1c). Not called yet.
        void UpdateInCombatAI() override {}
        void UpdateOutOfCombatAI() override {}
        void UpdateInCombatAI_Paladin() override {}
        void UpdateOutOfCombatAI_Paladin() override {}
        void UpdateInCombatAI_Shaman() override {}
        void UpdateOutOfCombatAI_Shaman() override {}
        void UpdateInCombatAI_Hunter() override {}
        void UpdateOutOfCombatAI_Hunter() override {}
        void UpdateInCombatAI_Mage() override {}
        void UpdateOutOfCombatAI_Mage() override {}
        void UpdateInCombatAI_Priest() override {}
        void UpdateOutOfCombatAI_Priest() override {}
        void UpdateInCombatAI_Warlock() override {}
        void UpdateOutOfCombatAI_Warlock() override {}
        void UpdateInCombatAI_Warrior() override {}
        void UpdateOutOfCombatAI_Warrior() override {}
        void UpdateInCombatAI_Rogue() override {}
        void UpdateOutOfCombatAI_Rogue() override {}
        void UpdateInCombatAI_Druid() override {}
        void UpdateOutOfCombatAI_Druid() override {}

    private:
        void Initialize();
        void EquipForTier();
        void RefreshAppearanceForNearbyPlayers();
        void UpdateSocial(uint32 diff);
        void UpdateIdle();
        void DeclineInviteNow();

        SpawnRequest m_request;
        std::string m_name;
        std::shared_ptr<BotHandle> m_handle;

        ShortTimeTracker m_updateTimer;
        uint32 m_nextIdleAtMs = 0;      // WorldTimer::getMSTime()
        // m_initialized comes from CombatBotBaseAI.

        // Pending refusals (ms countdowns, 0 = none).
        uint32 m_declineInviteInMs = 0;
        uint32 m_cancelTradeInMs = 0;
    };
}

#endif
