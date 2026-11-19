/*
 * World population bot: a temporary character (never saved) that only adds atmosphere.
 * It refuses trades and duels, and group invites unless it is a roster member invited by a real
 * player (then it is recruited as a companion, Companions/CompanionMgr). Phase 1a behaviour is idle (emotes, sitting,
 * turning); city and field behaviours come in phases 1b / 1c.
 *
 * Threads: UpdateAI and OnPacketReceived run on the map thread. Settings come from
 * PopulationMgr atomics; nothing else of the manager is touched here.
 */

#ifndef MANGOS_POPULATION_BOT_AI_H
#define MANGOS_POPULATION_BOT_AI_H

#include "TeamBotAI.h"
#include "Population/PopulationTypes.h"
#include "Life/Errands/LootRun.h"
#include "Population/TownLife.h"
#include "Timer.h"
#include <memory>

namespace Population
{
    // A TeamBotAI: a field bot is a team of one the Commander fights with (Commander/Team.h,
    // unit SpawnRequest::unitId) and hunts for (HuntAssigner). City bots never fight: no team.
    class PopulationBotAI : public TeamBotAI
    {
    public:
        PopulationBotAI(SpawnRequest const& request, std::string name, std::shared_ptr<BotHandle> handle);
        ~PopulationBotAI() override;

        bool OnSessionLoaded(PlayerBotEntry* entry, WorldSession* sess) override;
        void OnPlayerLogin() override;
        void UpdateAI(uint32 const diff) override;
        void OnPacketReceived(WorldPacket const* packet) override;

        // No player leads it.
        Player* GetPartyLeader() const override { return nullptr; }

        SpawnRequest const& GetRequest() const { return m_request; }
        std::string const& GetBotName() const { return m_name; }
        bool IsInitialized() const { return m_initialized; }

        // Dev review of actions (.popbot emote, addon window /pbemote): clears the previous action,
        // faces the viewer, shows one action and holds still for REVIEW_HOLD_MS (no idle
        // behaviour) so it can be watched. Text emotes are sent as the bot's own /emote, at the viewer.
        enum class ReviewKind : uint8 { OneShot, State, Stand, Text, Sheath };
        static constexpr uint32 REVIEW_HOLD_MS = 5 * 60 * 1000;
        void PlayForReview(ReviewKind kind, uint32 id, Player* viewer);
        void EndReview();

        // Field bots: fights before it moves on (logs out out of sight).
        static constexpr uint32 FIELD_FIGHTS_MIN = 5;
        static constexpr uint32 FIELD_FIGHTS_MAX = 15;
        // Nothing to hunt for this long: moves on.
        static constexpr uint32 FIELD_IDLE_LEAVE_MS = 60 * 1000;

    private:
        // Field bot tick: the Commander's body steps (TeamBotAI) around its own life: looting
        // after a fight (Life/Errands/LootRun.h), counting fights, moving on.
        void UpdateField(uint32 diff);
        void InitializeField();
        void Initialize();
        void EquipForTier();
        void HideFromNearbyPlayers();
        void UpdateSocial(uint32 diff);
        void UpdateIdle();
        void UpdateCrafter();
        // Standers, movers and sitters of a city or town live their day there (TownLife.h).
        bool LivesTownLife() const;
        void SetupProfession();
        void PickRecipe();
        void ClearCraftedItems();
        void MoveTo(float x, float y, float z);
        bool ReachedTarget() const;
        void FacePoint(float x, float y);
        void DeclineInviteNow();
        // Walk in from the entrance (logged in out of sight) / out to the leave spot.
        void UpdateEntering();
        void BeginLeaving();
        void UpdateLeaving();
        void ArriveAtSpot();
        void LogOut();

        SpawnRequest m_request;
        std::string m_name;
        std::shared_ptr<BotHandle> m_handle;

        // (m_updateTimer comes from TeamBotAI: the population interval in town, the
        // Commander's bot interval in the field.)
        uint32 m_nextIdleAtMs = 0;      // WorldTimer::getMSTime()
        uint32 m_reviewUntilMs = 0;     // != 0: held still for a review (PlayForReview)

        // Field
        ObjectGuid m_teamGuid;          // joined Commander::Team unit with this guid
        Life::LootRunState m_lootRun;
        bool m_wasInCombat = false;
        uint32 m_fights = 0;
        uint32 m_fightsToLeave = 0;

        // Coming in from out of sight, living at the spot, going out of sight to log out.
        enum class Phase : uint8 { Entering, Living, Leaving, Gone };
        Phase m_phase = Phase::Living;
        uint32 m_walkRetries = 0;
        // m_initialized comes from CombatBotBaseAI.

        // Movement target of the current MovePoint (walking in or out).
        float m_targetX = 0.0f, m_targetY = 0.0f, m_targetZ = 0.0f;
        uint32 m_moveDeadlineMs = 0;
        float m_homeX = 0.0f, m_homeY = 0.0f, m_homeZ = 0.0f;   // the spawn spot

        TownLife m_townLife;

        // Crafter: casts a real recipe in batches (reagents are added, products thrown away).
        uint32 m_professionSkill = 0;
        uint32 m_recipeSpell = 0;
        uint32 m_batchLeft = 0;
        uint32 m_craftRestUntilMs = 0;
        uint32 m_craftFailures = 0;

        // Pending refusals (ms countdowns, 0 = none).
        uint32 m_declineInviteInMs = 0;
        uint32 m_cancelTradeInMs = 0;
    };
}

#endif
