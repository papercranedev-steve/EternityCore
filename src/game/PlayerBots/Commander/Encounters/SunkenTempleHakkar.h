/*
 * PartyBot Commander boss module: the Avatar of Hakkar event, Sunken Temple (boss doc 2.1, P1).
 *
 * Event (event_scripts 8502, generic_scripts 8502xx, creature_ai_events 8440 / 8497): the
 * player starts it at the Evil God Summoning Circle with the Egg of Hakkar; instance data
 * TYPE_AVATAR (8) is IN_PROGRESS until the Avatar dies (DONE) or the event fails. Meanwhile:
 *   - Hakkari Minions (8437) come out of the circles every few seconds (trash);
 *   - every 60-110 s a Hakkari Bloodkeeper (8438, elite): its blood puts out one of the four
 *     Eternal Flames (the player's job); the fourth brings the Avatar (8443);
 *   - every 60-110 s three Nightmare Suppressors (8497, elite) walk in from the two doors to
 *     the Shade of Hakkar (8440) and channel Suppression on it; hit three times, the event
 *     fails. They fight whoever they meet, else they only walk.
 * The Avatar's Cause Insanity (mind control) is the generic AllyCharm's.
 *
 * Match: map 109 and the event on (instance data, read in Refresh) or the Avatar or one of
 *   the event's summons among the enemies.
 * Eyes (ListEnemies): the Suppressors within SEARCH_YD of the party, so the walking ones can
 *   be named.
 * Doctrine (ShapeDoctrine): kill order tiers: Suppressors first, then Bloodkeepers.
 * Duties (OfferDuties): one kill need per walking Suppressor (EncounterHelpers::AddKillNeed),
 *   SUPPRESSOR_POINTS, the one closest to the Shade first, never the main tank.
 * Rest (ForbidsRest): no sitting down while the event is on, unless nothing fought for
 *   QUIET_STALE_MS (EncounterHelpers::WaveWatch).
 * Voice: a line when Suppressors show up.
 *
 * Known limits:
 *   - The mana budget does not count the waves still to come (boss doc G5).
 *   - Putting out the flames is the player's (the bots do not pick up the blood).
 *   - Suppressors that already channel are not told apart from walking ones.
 */

#ifndef MANGOS_COMMANDER_ENCOUNTER_SUNKEN_TEMPLE_HAKKAR_H
#define MANGOS_COMMANDER_ENCOUNTER_SUNKEN_TEMPLE_HAKKAR_H

#include "Commander/Encounters/IEncounter.h"
#include "Commander/Encounters/EncounterHelpers.h"
#include "Commander/Perception/WorldModel.h"
#include "ObjectGuid.h"
#include <vector>

namespace Commander
{
    class SunkenTempleHakkar : public IEncounter
    {
    public:
        static constexpr uint32 MAP_ID = 109;
        static constexpr uint32 DATA_AVATAR = 8;            // sunken_temple.h TYPE_AVATAR
        static constexpr uint32 STATE_IN_PROGRESS = 1;      // ScriptedInstance.h EncounterState
        static constexpr uint32 NPC_AVATAR = 8443;
        static constexpr uint32 NPC_SHADE = 8440;
        static constexpr uint32 NPC_BLOODKEEPER = 8438;
        static constexpr uint32 NPC_MINION = 8437;
        static constexpr uint32 NPC_SUPPRESSOR = 8497;

        static constexpr float SEARCH_YD = 90.0f;
        // A failed event: the whole summoning again (several minutes, a likely wipe on the way).
        static constexpr float SUPPRESSOR_POINTS = 200.0f;
        static constexpr uint32 QUIET_STALE_MS = 25000;

        static constexpr int8 TIER_SUPPRESSOR = KILL_TIER_ROLE + 2;
        static constexpr int8 TIER_BLOODKEEPER = KILL_TIER_ROLE + 1;

        char const* GetId() const override { return "hakkar"; }
        bool Matches(WorldModel const& world, uint32 mapId) const override;
        void Refresh(TeamBotAI* bot, WorldModel const& world) override;
        void ListEnemies(TeamBotAI* bot, WorldModel const& world, std::vector<Creature*>& out) const override;
        void ShapeDoctrine(WorldModel const& world, TeamPlan& plan) override;
        void OfferDuties(WorldModel const& world, TeamPlan const& plan, std::vector<DutyNeed>& needs) const override;
        bool ForbidsRest() const override { return m_noRest; }
        void OnReset() override;

    private:
        static bool IsEventEntry(uint32 entry);

        bool m_eventOn = false;
        std::vector<ObjectGuid> m_suppressors;  // alive, within SEARCH_YD, this refresh
        Position3 m_shadePos;
        bool m_haveShade = false;
        EncounterHelpers::WaveWatch m_waves;
        bool m_noRest = false;
        bool m_startNoted = false;
    };
}

#endif
