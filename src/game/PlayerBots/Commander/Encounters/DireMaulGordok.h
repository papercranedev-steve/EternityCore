/*
 * PartyBot Commander boss module: King Gordok and Cho'Rush the Observer, Dire Maul North (boss doc
 * 2.2, P2).
 *
 * Script (boss_gordok_king.cpp, instance_dire_maul.cpp OnCreatureDeath): the two are linked
 * (either pulls the other). Cho'Rush fights as a mage, a shaman or a priest (the instance rolls
 * his gear: TYPE_CHORUSH_EQUIPMENT), heals the King as shaman or priest. When the King dies,
 * Cho'Rush turns friendly; when Cho'Rush dies, he counts as a dead guard for the Gordok tribute
 * (fewer guards alive, a poorer tribute). So the King dies first and Cho'Rush is left alive.
 * The King has an ability sheet (BossSheets.cpp); Cho'Rush none (his spells depend on the roll).
 *
 * Match: map 429 and the King (11501) or Cho'Rush (14324) among the enemies.
 *
 * Doctrine (ShapeDoctrine): the King one tier above the generic kill order (KILL_TIER_ROLE + 1);
 *   while the King lives, Cho'Rush STAMP_DAMAGE_LIMIT (no hits at or below SPARE_STOP_PCT, none
 *   that take him below SPARE_KEEP_PCT) and the last kill tier.
 *
 * Known limits:
 *   - His heals on the King are interrupted by the generic interrupters only if they see the
 *     cast (live casts are seen; his other spells are not forecast).
 *   - The player is not held: he can still kill Cho'Rush.
 */

#ifndef MANGOS_COMMANDER_ENCOUNTER_DIRE_MAUL_GORDOK_H
#define MANGOS_COMMANDER_ENCOUNTER_DIRE_MAUL_GORDOK_H

#include "Commander/Encounters/IEncounter.h"
#include "Commander/Coordination/TeamPlan.h"
#include "ObjectGuid.h"

namespace Commander
{
    class DireMaulGordok : public IEncounter
    {
    public:
        static constexpr uint32 MAP_ID = 429;
        static constexpr uint32 NPC_KING = 11501;
        static constexpr uint32 NPC_CHORUSH = 14324;
        static constexpr float SPARE_STOP_PCT = 30.0f;
        static constexpr float SPARE_KEEP_PCT = 15.0f;
        static constexpr int8 TIER_KING = KILL_TIER_ROLE + 1;
        static constexpr int8 TIER_LAST = -1;

        char const* GetId() const override { return "dm-gordok"; }
        bool Matches(WorldModel const& world, uint32 mapId) const override;
        void Refresh(TeamBotAI* bot, WorldModel const& world) override;
        void ShapeDoctrine(WorldModel const& world, TeamPlan& plan) override;
        void OnReset() override;

    private:
        ObjectGuid m_king;
        ObjectGuid m_chorush;
        bool m_noted = false;
    };
}

#endif
