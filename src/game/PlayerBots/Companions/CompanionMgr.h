/*
 * Companions: roster members a real player recruited ("given a soul"). The temporary city bot
 * becomes a real saved character on a dedicated account and logs in again as a party bot
 * (PartyBotAI). From then on the database is the truth for level, gear, money and quests.
 * Design: .kiro/docs/2026-11-17-soul-companions.md, stage S1 (workspace root).
 *
 * Recruiting swaps the AI by logging the character out and in again (no live AI swap):
 * the temporary bot leaves, then Update logs the saved character in next to the recruiter.
 *
 * World thread only, except IsEnabled (atomic, read by population bot AIs).
 * Table (characters DB, created on load): custom_companion.
 */

#ifndef MANGOS_COMPANIONS_COMPANION_MGR_H
#define MANGOS_COMPANIONS_COMPANION_MGR_H

#include "Common.h"
#include "ObjectGuid.h"
#include <atomic>
#include <string>
#include <unordered_map>
#include <vector>

class Player;

namespace Companions
{
    // Who picks a quest reward when the companion turns a quest in (RewardChoice.h).
    enum class RewardMode : uint8
    {
        Auto = 0,       // the companion picks (default)
        AskPlayer = 1,  // the player picks; later unlocked by affinity (soul companions doc §4.3)
    };

    struct CompanionInfo
    {
        RewardMode rewardMode = RewardMode::Auto;
        uint32 charGuid = 0;
        uint32 memberId = 0;        // custom_popbot_member.id, 0 = not from the roster
        uint32 recruiterGuid = 0;   // real character that recruited it
        uint32 recruitedAt = 0;     // unix time
        // Personality tag keys (Personality.h), from the roster member (custom_popbot_member_tag).
        // Set at load / recruit, read only afterwards (map threads read it).
        std::vector<std::string> tags;
    };

    class CompanionMgr
    {
    public:
        static CompanionMgr& Instance();

        // Companion.* (called from PlayerBotMgr::LoadConfig).
        void LoadConfig();
        // Table, companions and the owning account (called from PlayerBotMgr::Load).
        void Load();
        void Update(uint32 diff);

        bool IsEnabled() const { return m_enabled.load(std::memory_order_relaxed); }
        bool IsCompanion(uint32 charGuid) const { return m_companions.find(charGuid) != m_companions.end(); }
        CompanionInfo const* Find(uint32 charGuid) const;
        // Auto for anyone that is not a companion.
        RewardMode GetRewardMode(uint32 charGuid) const;
        // World thread (a command, later the affinity system); stored at once.
        void SetRewardMode(uint32 charGuid, RewardMode mode);

        // Saves an online temporary bot as a real character and queues its login as a party bot
        // of the recruiter. The caller logs the temporary bot out afterwards. False: nothing changed.
        bool Recruit(Player* bot, Player* recruiter, uint32 memberId);

    private:
        CompanionMgr() = default;

        bool EnsureTable();
        static std::vector<std::string> LoadTags(uint32 memberId);
        // Account id of Companion.Account; creates the account the first time (async, so 0 until it exists).
        uint32 ResolveAccount();
        void LoginAsPartyBot(uint32 charGuid, Player* leader);

        struct PendingLogin
        {
            uint32 charGuid = 0;
            ObjectGuid recruiter;
            uint32 sinceMs = 0;
        };

        std::atomic<bool> m_enabled{ true };
        std::string m_accountName = "COMPANIONS";
        uint32 m_accountId = 0;
        bool m_accountRequested = false;
        bool m_available = false;

        uint32 m_nowMs = 0;
        std::unordered_map<uint32, CompanionInfo> m_companions;
        std::vector<PendingLogin> m_pending;
    };
}

#define sCompanionMgr Companions::CompanionMgr::Instance()

#endif
