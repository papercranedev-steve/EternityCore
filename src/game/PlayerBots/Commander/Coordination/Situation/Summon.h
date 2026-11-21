/*
 * PartyBot Commander layer 2 (situation): warlocks of the team summon members who are far away:
 * the player who went to town while the team waited (Stay.h; the companion asks him first,
 * Companions/Travel/SummonOffer) and members left behind (GatherPlanner.h). Design
 * .kiro/docs/2026-11-21-companion-talk.md §5, §10, §11 (workspace root).
 *
 * The ritual (VMaNGOS): the warlock casts Ritual of Summoning on its selected target and channels;
 * the portal needs two more of its group clicking it, each channelling too; then the target gets
 * the summon confirmation (2 minutes). Anyone who moves or casts breaks the channel. No cooldown,
 * one Soul Shard each. So the plan says who does what (TeamPlan::summon: one ritual per warlock,
 * several at once in a raid, nobody in two) and each of them keeps to it until it ends; the
 * hands (casting, walking to the portal, clicking, a bot accepting) are the bot's
 * (PartyBotAI::UpdateSummon).
 *
 * Map thread of the party.
 */

#ifndef MANGOS_COMMANDER_SUMMON_H
#define MANGOS_COMMANDER_SUMMON_H

#include "Common.h"
#include "ObjectGuid.h"
#include <vector>

class Player;

namespace Commander
{
    struct TeamPlan;

    enum class SummonRole : uint8
    {
        None,
        Caster,
        Helper,
    };

    namespace Summon
    {
        uint32 constexpr RITUAL_OF_SUMMONING = 698;
        uint32 constexpr SOUL_SHARD = 6265;
        // A teammate is summoned while one shard stays (for a Soulstone); the player with the last.
        uint32 constexpr SHARDS_KEPT_FOR_TEAMMATES = 1;
        // A target whose summon went out is not summoned again for this long (it may accept).
        uint32 constexpr SENT_HOLD_MS = 130 * IN_MILLISECONDS;
        // A teammate far enough to be summoned: another map, or this far on the same one (more
        // than a companion's town trip goes from the leader, Life/Errands/TownTrip TRIP_YD 150).
        float constexpr FAR_YD = 300.0f;

        // A ritual of `warlock` for `target`, clicked by `helpers`. False: one of them is in a
        // ritual already, or the target is being summoned.
        bool Start(TeamPlan& plan, Player const* warlock, ObjectGuid target, std::vector<ObjectGuid> const& helpers);
        // The warlock's ritual is over. `leaderBack`: its target was the player the team waited
        // for and he came (everyone follows again).
        void End(TeamPlan& plan, ObjectGuid warlock, bool leaderBack);
        // The warlock's summon went out to the target (it may accept for 2 minutes).
        void MarkSent(TeamPlan& plan, ObjectGuid target, uint32 nowMs);
        bool RecentlySent(TeamPlan const& plan, ObjectGuid target, uint32 nowMs);
        SummonRole RoleOf(TeamPlan const& plan, ObjectGuid member);
        // Knows the ritual, has a shard more than `keep`.
        bool CanCast(Player const* warlock, uint32 keep);
    }
}

#endif
