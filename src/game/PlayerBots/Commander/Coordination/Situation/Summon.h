/*
 * PartyBot Commander layer 2 (situation): a warlock of the team summons a member who is far away
 * (the player who went to town while the team waited: Stay.h). Design
 * .kiro/docs/2026-11-21-companion-talk.md §5, §10 (workspace root).
 *
 * The ritual (VMaNGOS): the warlock casts Ritual of Summoning on its selected target and channels;
 * the portal needs two more of its group clicking it, each channelling too; then the target gets
 * the summon confirmation (2 minutes). Anyone who moves or casts breaks the channel. So the plan
 * says who does what (TeamPlan::summon) and each of them keeps to it until it ends; the hands
 * (casting, walking to the portal, clicking) are the bot's (PartyBotAI::UpdateSummon).
 *
 * Who may be asked and when (affinity, the question to the player) is the caller's.
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

        // A ritual of `warlock` for `target`, clicked by `helpers`. False: one is under way.
        bool Start(TeamPlan& plan, Player const* warlock, ObjectGuid target, std::vector<ObjectGuid> const& helpers);
        // It is over. `arrived`: the target came (the team that waited for him follows again).
        void End(TeamPlan& plan, bool arrived);
        SummonRole RoleOf(TeamPlan const& plan, ObjectGuid member);
    }
}

#endif
