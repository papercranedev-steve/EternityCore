/*
 * PartyBot Commander boss module: Golem Lord Argelmach, Blackrock Depths (boss doc 2.2, P2).
 *
 * Script (blackrock_depths.cpp npc_golem_lord_argelmachAI, instance DATA_ARGELMACH_AGGRO): when
 * he is pulled, every Ragereaver Golem (8906) and Wrath Hammer Construct (8907) of the instance
 * still alive within 80 yd of him attacks his victim. His own spells are on his spell list.
 *
 * Before the pull (BeforePull, boss doc G9): the golems and constructs idle within CALL_RANGE_YD
 * of him are a BossPrep: the clear passes over him until they are dead, and the party tells the
 * player when he comes near (voice.boss.argelmach.golems). Nothing in the fight itself: the adds
 * that come anyway are the generic modules'.
 *
 * Known limits:
 *   - Only what the pack scan sees (PACK_SCAN_RADIUS around the leader): golems farther from
 *     the leader than that are not counted.
 */

#ifndef MANGOS_COMMANDER_ENCOUNTER_BLACKROCK_DEPTHS_ARGELMACH_H
#define MANGOS_COMMANDER_ENCOUNTER_BLACKROCK_DEPTHS_ARGELMACH_H

#include "Commander/Encounters/IEncounter.h"

namespace Commander
{
    class BlackrockDepthsArgelmach : public IEncounter
    {
    public:
        static constexpr uint32 MAP_ID = 230;
        static constexpr uint32 NPC_ARGELMACH = 8983;
        static constexpr uint32 NPC_RAGEREAVER = 8906;
        static constexpr uint32 NPC_WRATH_HAMMER = 8907;
        static constexpr float CALL_RANGE_YD = 80.0f;

        char const* GetId() const override { return "brd-argelmach"; }
        // Never the fight's module: it only speaks before the pull.
        bool Matches(WorldModel const& /*world*/, uint32 /*mapId*/) const override { return false; }
        void BeforePull(WorldModel const& world, std::vector<BossPrep>& out) const override;
        void OnReset() override {}
    };
}

#endif
