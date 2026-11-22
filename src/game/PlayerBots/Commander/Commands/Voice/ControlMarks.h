/*
 * PartyBot Commander: raid target marks on what the bots crowd-control (user 2026-11-22), so the
 * player sees at a glance what is sheeped or sapped and leaves it alone. Part of the party voice
 * (PartyVoice.h): the line "我把月亮羊了" names the mark this puts.
 *
 * Marked: the pull's opening control target (from when it is decided, before the pull), control
 * duties about to be cast (TeamPlan::controls), and enemies under a control a party bot (or its
 * pet) cast. The mark follows the caster's class as players use them: mage moon, rogue star,
 * priest triangle, hunter square, druid circle, warlock diamond; taken: the next free one of
 * those. Skull and cross are never used (players' kill order).
 *
 * The player's own marks are left alone: an enemy he marked is not marked again, an icon he put
 * on a living creature of this map is not taken. Ours are cleared once the enemy is no longer
 * controlled or planned for RELEASE_MS (death, control over), or at once when the player turns
 * marks off. Group::SetTargetIcon sends each change to the group. Map thread of the party.
 */

#ifndef MANGOS_COMMANDER_CONTROL_MARKS_H
#define MANGOS_COMMANDER_CONTROL_MARKS_H

#include "Common.h"
#include "ObjectGuid.h"
#include <vector>

class Player;

namespace Commander
{
    struct WorldModel;
    struct TeamPlan;

    class ControlMarks
    {
    public:
        static constexpr uint32 RELEASE_MS = 2000;
        static constexpr int8 NONE = -1;

        // `anyMember`: a bot of the party on the party's map (its group and map). `on`: the
        // human's setting (VoiceSettings::marks).
        void Step(Player* anyMember, WorldModel const& world, TeamPlan const& plan, bool on, uint32 nowMs);
        // The icon (RaidTargetIcon) our marks put on `enemy`, NONE if none.
        int8 IconOf(ObjectGuid enemy) const;

    private:
        struct Mark
        {
            ObjectGuid enemy;
            uint8 icon = 0;
            uint32 lastWantedMs = 0;
        };
        std::vector<Mark> m_marks;
    };
}

#endif
