/*
 * PartyBot Commander boss module: Captain Kromcrush, Dire Maul North (boss doc 2.2, P2).
 *
 * Script (instance_dire_maul.cpp boss_kromcrush): below 25% he casts Retaliation (22857, 15 s, a
 * proc aura that strikes back at melee hits) once; below 50% he calls two Gordok Reavers. His
 * other spells and the fear are on his ability sheet (BossSheets.cpp). Retaliation is his only
 * (no other creature casts 22857 or the warrior's 20230), so its handling is this module's.
 *
 * Match: map 429 and Kromcrush (14325) among the enemies.
 *
 * Doctrine (ShapeDoctrine): while Retaliation is on him (read live in Refresh) he is stamped
 *   STAMP_NO_MELEE: nobody, the tank included, hits him in melee (TeamPlan::MeleeBlock); ranged
 *   and spells go on. A line to the players when it goes up (voice.boss.kromcrush.retaliation).
 *
 * Known limits:
 *   - The tank's threat is not topped up before it (it stops hitting for 15 s; Kromcrush keeps
 *     him as long as nobody pulls ahead).
 *   - The player is not held.
 */

#ifndef MANGOS_COMMANDER_ENCOUNTER_DIRE_MAUL_KROMCRUSH_H
#define MANGOS_COMMANDER_ENCOUNTER_DIRE_MAUL_KROMCRUSH_H

#include "Commander/Encounters/IEncounter.h"
#include "Commander/Coordination/TeamPlan.h"
#include "ObjectGuid.h"

namespace Commander
{
    class DireMaulKromcrush : public IEncounter
    {
    public:
        static constexpr uint32 MAP_ID = 429;
        static constexpr uint32 NPC_KROMCRUSH = 14325;
        static constexpr uint32 SPELL_RETALIATION = 22857;

        char const* GetId() const override { return "dm-kromcrush"; }
        bool Matches(WorldModel const& world, uint32 mapId) const override;
        void Refresh(TeamBotAI* bot, WorldModel const& world) override;
        void ShapeDoctrine(WorldModel const& world, TeamPlan& plan) override;
        void OnReset() override;

    private:
        ObjectGuid m_boss;
        bool m_retaliation = false;
        bool m_said = false;
    };
}

#endif
