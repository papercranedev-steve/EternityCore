/*
 * PartyBot Commander layer 3 (cerebellum): which member a healer tops up out of combat (user
 * 2026-11-22). Until then the out of combat heal (TeamBotAI::FindAndHealInjuredAlly with the
 * rotations' 100%) healed anyone missing one point: after a stamina buff (Fortitude, Mark of
 * the Wild, Blood Pact raise max health), on every step in and out of the imp's Blood Pact,
 * and at levels 1-5 for a scratch that natural regeneration fills in seconds.
 *
 * A member not in combat is healed only when all three hold:
 *   deficit  missing >= min(the most mana-efficient heal x (1 - HealPlan::OVERHEAL_MAX),
 *            max health x MAX_DEFICIT_PCT): one heal not more than a fifth wasted, capped so a
 *            low level member (one heal ~ its whole health) need not sink to nothing
 *   regen    it would not be full by itself within WAIT_SEC (Perception/Values/HealthRegen:
 *            spirit regeneration, sitting, food it is eating)
 *   stable   the gap has stood STABLE_MS with the same max health: a max health that just
 *            changed (a buff landing or fading, stepping in or out of an aura) waits
 * A member in combat (the party fights nothing the Commander sees, e.g. a player pulled alone)
 * is healed as before, below the caller's percent. Of the members to heal: the lowest percent,
 * a non-tank another healer already casts on left out (as CombatBotBaseAI::SelectHealTarget).
 *
 * Map thread of the bot.
 */

#ifndef MANGOS_COMMANDER_REST_TOP_UP_H
#define MANGOS_COMMANDER_REST_TOP_UP_H

#include "Common.h"
#include "ObjectGuid.h"
#include <map>

class TeamBotAI;
class Unit;

namespace Commander
{
    // Per bot (TeamBotAI member). Plain data.
    struct RestTopUpState
    {
        struct Gap
        {
            uint32 maxHealth = 0;
            uint32 sinceMs = 0;     // first seen with this max health
        };
        std::map<ObjectGuid, Gap> gaps;   // members whose gap passes deficit and regen
    };

    namespace RestTopUp
    {
        static constexpr float WAIT_SEC = 8.0f;
        static constexpr float MAX_DEFICIT_PCT = 50.0f;
        static constexpr uint32 STABLE_MS = 2500;

        // Missing health at which a member out of combat is worth a heal (header: deficit).
        float Deficit(float maxHealth, float efficientHeal);

        // The member to heal now (header), null = none. `selfPercent` / `groupPercent`: the
        // caller's thresholds, used as before for members in combat and as the outer limit.
        Unit* Select(TeamBotAI const* bot, RestTopUpState& state, float selfPercent, float groupPercent);
    }
}

#endif
