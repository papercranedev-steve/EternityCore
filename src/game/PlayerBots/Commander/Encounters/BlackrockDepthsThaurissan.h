/*
 * PartyBot Commander boss module: Emperor Dagran Thaurissan and Princess Moira Bronzebeard,
 * Blackrock Depths (boss doc 2.2, P2).
 *
 * Script (boss_emperor_dagran_thaurissan.cpp): Moira heals the Emperor every 10 s while he is
 * hurt and casts Mind Blast, Shadow Word: Pain and Smite; when he dies she turns friendly. Both
 * have ability sheets (BossSheets.cpp), so her Heal makes her a healer for the generic kill
 * order and the interrupters.
 *
 * Both factions' quests need her alive: "The Royal Rescue" (4003, Horde) and "The Fate of the
 * Kingdom" (4362, Alliance) end by talking to her after the Emperor dies (the boss doc's "Horde
 * may kill her first" was wrong, quest_template read 2026-11-28).
 *
 * Match: map 230 and the Emperor (9019) or Moira (8929) among the enemies.
 *
 * Doctrine (ShapeDoctrine), only while a player of the party has one of the quests incomplete:
 *   Moira gets STAMP_DAMAGE_LIMIT (no hits at or below SPARE_STOP_PCT, none that take her below
 *   SPARE_KEEP_PCT) and the last kill tier (KILL_TIER_LAST): the Emperor first. Without the
 *   quest the generic order applies (her heals: a healer, killed first).
 *   A line to the players once (voice.boss.moira.spare).
 *
 * Known limits:
 *   - Interrupting her heals is the generic interrupters' (an interrupt that hurts her is
 *     still held at the line).
 *   - The player is not held: he can still kill her.
 */

#ifndef MANGOS_COMMANDER_ENCOUNTER_BLACKROCK_DEPTHS_THAURISSAN_H
#define MANGOS_COMMANDER_ENCOUNTER_BLACKROCK_DEPTHS_THAURISSAN_H

#include "Commander/Encounters/IEncounter.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/WorldModel.h"
#include "ObjectGuid.h"

namespace Commander
{
    class BlackrockDepthsThaurissan : public IEncounter
    {
    public:
        static constexpr uint32 MAP_ID = 230;
        static constexpr uint32 NPC_EMPEROR = 9019;
        static constexpr uint32 NPC_MOIRA = 8929;
        static constexpr uint32 QUEST_ROYAL_RESCUE = 4003;      // Horde
        static constexpr uint32 QUEST_FATE_OF_KINGDOM = 4362;   // Alliance

        static constexpr float SPARE_STOP_PCT = 30.0f;
        static constexpr float SPARE_KEEP_PCT = 15.0f;
        static constexpr int8 KILL_TIER_LAST = -1;

        char const* GetId() const override { return "brd-thaurissan"; }
        bool Matches(WorldModel const& world, uint32 mapId) const override;
        void Refresh(TeamBotAI* bot, WorldModel const& world) override;
        void ShapeDoctrine(WorldModel const& world, TeamPlan& plan) override;
        void OnReset() override;

    private:
        ObjectGuid m_moira;
        bool m_spare = false;           // a player of the party needs her alive
        bool m_spareSaid = false;
    };
}

#endif
