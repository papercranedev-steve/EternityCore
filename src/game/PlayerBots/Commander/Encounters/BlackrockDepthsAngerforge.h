/*
 * PartyBot Commander boss module: General Angerforge, Blackrock Depths (boss doc 2.1, P1).
 *
 * Script (boss_general_angerforge.cpp): below 30% health he calls 8 Anvilrage Reservists
 * (8901) and 2 Anvilrage Medics (8894, elite healers, flee at 15%) at the room's back; again
 * every 3 minutes while he stays below 30%. His Sunder Armor is on his ability sheet
 * (BossSheets.cpp).
 *
 * Match: map 230 and Angerforge (9033) or one of the summoned reservists / medics among the
 * enemies (the summons outlive him).
 *
 * Doctrine (ShapeDoctrine):
 *   Hold before the call: while SUMMON_PCT < boss <= HOLD_BAND_PCT, no call yet, and (another
 *     enemy alive || tank below TANK_READY_PCT || lowest healer mana below
 *     HEALER_READY_MANA_PCT), capped at MAX_HOLD_MS, the boss gets STAMP_DAMAGE_LIMIT: stop
 *     at HOLD_BAND_PCT, no hit past SUMMON_PCT, his tank keeps hitting (as VanCleef).
 *   After the call, kill order tiers (DoctrinePlan::SetKillTier): the boss first when the
 *     party kills him within BURN_SEC (Value::DealtDps), then the medics (they heal him and
 *     the reservists), the reservists by the generic focus. The tank's area threat and the
 *     area tactic on the reservists are the generic modules'.
 *
 * Known limits:
 *   - The player is not held: if he keeps hitting, the boss crosses 30% anyway.
 *   - The burn check uses the party's damage now, not how much of it is free once the
 *     reservists arrive.
 *   - Pull ready thresholds are not raised for this fight (boss doc G9).
 */

#ifndef MANGOS_COMMANDER_ENCOUNTER_BLACKROCK_DEPTHS_ANGERFORGE_H
#define MANGOS_COMMANDER_ENCOUNTER_BLACKROCK_DEPTHS_ANGERFORGE_H

#include "Commander/Encounters/IEncounter.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/WorldModel.h"
#include "ObjectGuid.h"
#include <vector>

namespace Commander
{
    class BlackrockDepthsAngerforge : public IEncounter
    {
    public:
        static constexpr uint32 MAP_ID = 230;
        static constexpr uint32 NPC_ANGERFORGE = 9033;
        static constexpr uint32 NPC_MEDIC = 8894;
        static constexpr uint32 NPC_RESERVIST = 8901;

        // The script's call line and the hold band above it.
        static constexpr float SUMMON_PCT = 30.0f;
        static constexpr float HOLD_BAND_PCT = 33.0f;
        // Ready to push: tank health and lowest healer mana at least this (ten adds: more mana
        // than VanCleef's two).
        static constexpr float TANK_READY_PCT = 80.0f;
        static constexpr float HEALER_READY_MANA_PCT = 50.0f;
        // Give up holding after this long (mana barely comes back in a fight, he keeps hitting
        // the tank meanwhile).
        static constexpr uint32 MAX_HOLD_MS = 15000;
        // After the call: the boss stays the first kill when he dies this fast.
        static constexpr float BURN_SEC = 10.0f;

        static constexpr int8 TIER_BOSS_BURN = KILL_TIER_ROLE + 2;
        static constexpr int8 TIER_MEDIC = KILL_TIER_ROLE + 1;

        char const* GetId() const override { return "angerforge"; }
        bool Matches(WorldModel const& world, uint32 mapId) const override;
        void Refresh(TeamBotAI* bot, WorldModel const& world) override;
        void ShapeDoctrine(WorldModel const& world, TeamPlan& plan) override;
        void OnReset() override;

    private:
        static bool IsCalled(EnemyFacts const& e);

        ObjectGuid m_boss;
        bool m_bossAlive = false;
        float m_bossPct = 100.0f;
        bool m_burn = false;                    // after the call: the boss dies within BURN_SEC
        std::vector<ObjectGuid> m_medics;       // alive summoned medics, this refresh
        uint32 m_calledAtMs = 0;                // first call seen this fight, 0 = none yet

        bool m_holdDamage = false;
        bool m_holdGivenUp = false;             // MAX_HOLD_MS reached: no more holds this fight
        uint32 m_holdSinceMs = 0;
    };
}

#endif
