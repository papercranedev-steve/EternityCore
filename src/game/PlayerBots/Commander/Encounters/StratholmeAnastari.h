/*
 * PartyBot Commander boss module: Baroness Anastari, Stratholme (strat-20261127-002326; design
 * .kiro/docs/2026-11-27-encounter-layers.md 1.2).
 *
 * Script (boss_baroness_anastari.cpp): Possess (17244, 120 s, not dispellable) on a random
 * player above 30% health; while it lasts she is invisible, not selectable and immune, and the
 * possessed member fights us. Every second the script checks the member: below 25% health the
 * possession ends and the health goes back to the share it had (as remembered by the script).
 * So the member is freed by hitting it down, not by killing her.
 *
 * Match: map 329 and Anastari (10436) among the enemies, or a party member she controls.
 *
 * Doctrine (ShapeDoctrine), per member she controls:
 *   - MEMBER_HIT_TO_FREE + MEMBER_DAMAGE_LIMIT: stop at STOP_PCT (below the script's 25%), no
 *     hit past KEEP_PCT (it must not die before the next check), no periodic damage; window
 *     WINDOW_SEC (the script's check plus the bots' reaction). TargetAssigner sends as many
 *     bots as fit in the window (ChooseFreeing), the cast checks and the hands-off stop do the
 *     rest (Constraints::HandsOff, TeamBotAI::StopHandsOffVictims).
 *   - The generic doctrine's "control the ally until the charmer dies" (AllyCharm.h, hold) is
 *     turned off: she cannot be killed meanwhile, and a control only drags it out.
 * Healers already leave a charmed member alone (HealPlanBehavior).
 *
 * Facts (AddFacts): her Silence (18327, around her, at once when a possession ends and every
 * 13-18 s) as a hazard zone (WorldModel::scriptedHazards): around her while she is there,
 * around the spot the script brings her back to while someone is possessed (she stands next
 * to the possessed member then, invisible). Healers and ranged stand clear (positioning).
 *
 * Known limits: the player's own damage is not limited; a pet's melee is called back only at
 * the stop line; the spot she returns to is where she stood on the refresh before her first
 * possession (the script remembers it at the cast).
 */

#ifndef MANGOS_COMMANDER_ENCOUNTER_STRATHOLME_ANASTARI_H
#define MANGOS_COMMANDER_ENCOUNTER_STRATHOLME_ANASTARI_H

#include "Commander/Encounters/IEncounter.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/WorldModel.h"
#include "ObjectGuid.h"
#include <vector>

namespace Commander
{
    class StratholmeAnastari : public IEncounter
    {
    public:
        static constexpr uint32 MAP_ID = 329;
        static constexpr uint32 NPC_ANASTARI = 10436;

        // The script frees the member below 25% (`< 25.0f`): the bots stop a little lower, so
        // a member left at exactly 25% is not stuck.
        static constexpr float FREE_BELOW_PCT = 25.0f;
        static constexpr float STOP_PCT = 24.0f;
        static constexpr float KEEP_PCT = 5.0f;
        // The script checks every 1 s; the bots see the line on their next tick.
        static constexpr float WINDOW_SEC = 1.5f;

        // Silence (18327): self cast every 13-18 s, and at once when a possession ends.
        static constexpr uint32 SPELL_SILENCE = 18327;
        static constexpr float SILENCE_REPEAT_SEC = 15.5f;
        // While someone is possessed the silence comes as soon as the member is freed: weighed
        // as this close, so casters already stand clear of her spot.
        static constexpr float SILENCE_SOON_SEC = 2.0f;

        char const* GetId() const override { return "anastari"; }
        bool Matches(WorldModel const& world, uint32 mapId) const override;
        void AddFacts(WorldModel& world) override;
        void ShapeDoctrine(WorldModel const& world, TeamPlan& plan) override;
        void OnReset() override;

    private:
        static bool IsPossessed(PartyMemberFacts const& m);

        std::vector<ObjectGuid> m_noted;    // members whose possession was noted this fight
        // Where she stood before her first possession: the script brings her back there every
        // time (it remembers the spot once), and silences around it.
        ObjectGuid m_boss;
        Position3 m_lastPos;
        Position3 m_home;
        bool m_seen = false;
        bool m_homeKnown = false;
        uint32 m_level = 0;
    };
}

#endif
