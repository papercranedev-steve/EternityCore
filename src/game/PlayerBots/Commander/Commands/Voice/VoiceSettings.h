/*
 * PartyBot Commander: what a player wants to hear from his party (`.partybot voice`, the PartyBot
 * addon's settings in the game's Interface Options). Kept per player by CommanderMgr, runtime
 * only: the addon sends it again after login. The party voice reads the human's (PartyVoice.h).
 */

#ifndef MANGOS_COMMANDER_VOICE_SETTINGS_H
#define MANGOS_COMMANDER_VOICE_SETTINGS_H

#include "Common.h"
#include <cstring>

namespace Commander
{
    // How talkative the party is (user 2026-11-22: 话痨程度).
    enum class VoiceLevel : uint8
    {
        Quiet,      // only what matters for the fight (Urgent, Essential)
        Normal,     // plus thanks and congratulations, some atmosphere from chatty companions
        Chatty,     // everything, idle talk more often
    };

    struct VoiceSettings
    {
        VoiceLevel level = VoiceLevel::Normal;
        bool marks = true;          // raid target marks on what the bots crowd-control (ControlMarks.h)

        bool IsDefault() const { return level == VoiceLevel::Normal && marks; }
    };

    inline char const* VoiceLevelName(VoiceLevel level)
    {
        switch (level)
        {
            case VoiceLevel::Quiet:  return "quiet";
            case VoiceLevel::Chatty: return "chatty";
            default:                 return "normal";
        }
    }

    inline bool ParseVoiceLevel(char const* text, VoiceLevel& out)
    {
        if (!text)
            return false;
        if (!strcmp(text, "quiet"))  { out = VoiceLevel::Quiet;  return true; }
        if (!strcmp(text, "normal")) { out = VoiceLevel::Normal; return true; }
        if (!strcmp(text, "chatty")) { out = VoiceLevel::Chatty; return true; }
        return false;
    }
}

#endif
