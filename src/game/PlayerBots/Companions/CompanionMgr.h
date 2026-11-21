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
#include "Companions/OfflineLife.h"
#include "Companions/PastProfessions.h"
#include <atomic>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class PartyBotAI;
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
        // A temporary bot that acts as a companion for a dev console test scenario (MarkTemporary):
        // in memory only, nothing about it is written to the database.
        bool temporary = false;
        // 0..MAX_AFFINITY: what the player did for it (class quests, design doc 2026-11-18 §6).
        // Through Get/AddAffinity only (map threads change it).
        uint32 affinity = 0;
        // Unix time it was last seen online (OfflineLife counts from it), 0 = never. World thread.
        uint32 lastSeen = 0;
        // Temporary companions only (PretendAway): the offline skill points of the day, which a
        // real companion keeps in custom_companion_skillup.
        uint32 temporaryDay = 0;
        std::map<uint32, uint32> temporarySkillUps;
    };

    uint32 const MAX_AFFINITY = 100;

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
        // World thread. Copies of everyone `recruiterGuid` recruited (temporary ones too), any order.
        std::vector<CompanionInfo> OfRecruiter(uint32 recruiterGuid) const;
        // Auto for anyone that is not a companion.
        RewardMode GetRewardMode(uint32 charGuid) const;
        // World thread (a command, later the affinity system); stored at once.
        void SetRewardMode(uint32 charGuid, RewardMode mode);

        // Any thread. Add: capped at MAX_AFFINITY, stored at once (not for temporary companions);
        // returns the new value (0: not a companion).
        uint32 GetAffinity(uint32 charGuid) const;
        uint32 AddAffinity(uint32 charGuid, uint32 points, char const* reason);

        // Saves an online temporary bot as a real character and queues its login as a party bot
        // of the recruiter. The caller logs the temporary bot out afterwards. False: nothing changed.
        bool Recruit(Player* bot, Player* recruiter, uint32 memberId);

        // Dev console test scenarios (lua/tests/life): a temporary bot (saving disabled) counts as a
        // companion of `recruiter` until it logs out (PlayerBotMgr calls ForgetTemporary). Never
        // saved, no rows written; it cannot be recruited while marked. World thread, maps idle.
        // False: not a temporary bot, or already a companion.
        bool MarkTemporary(Player* bot, Player* recruiter, std::vector<std::string> tags = {});
        // True if it was a temporary companion (and is no longer one).
        bool ForgetTemporary(uint32 charGuid);
        bool IsTemporary(uint32 charGuid) const
        {
            CompanionInfo const* info = Find(charGuid);
            return info && info->temporary;
        }

        // World thread. Its professions are given (PastProfessions); false for one recruited before
        // professions existed, until it is next online.
        bool ProfessionsDecided(uint32 charGuid) const;
        // World thread. Offline skill points it got today, skill -> points (custom_companion_skillup;
        // in memory for a temporary companion).
        std::map<uint32, uint32> SkillUpsToday(uint32 charGuid) const;

        // Dev console (u:offline, u:reprof), world thread; `bot` an online companion party bot.
        // As if it had been away `secs` and came back now: OfflineLife and its party line, like a
        // real arrival. False: not one.
        bool PretendAway(Player* bot, uint32 secs, OfflineLife::Result& result);
        // Forgets its professions and decides them again (PastProfessions, seeded by the character:
        // the same ones for a real companion). False: not one.
        bool RedecideProfessions(Player* bot, PastProfessions::Result& result);

    private:
        CompanionMgr() = default;

        bool EnsureTable();
        // custom_popbot_member_tag belongs to the roster (Population/Roster), created when it first loads.
        static bool HasTagTable();
        static std::vector<std::string> LoadTags(uint32 memberId);
        // Account id of Companion.Account; creates the account the first time (async, so 0 until it exists).
        uint32 ResolveAccount();
        void LoginAsPartyBot(uint32 charGuid, Player* leader);
        // Companions recruited before class quest abilities existed: decided once each is online
        // as a party bot (ClassQuests/PastQuests).
        void DecidePastQuestsOfOnline();
        // Companions recruited before professions existed: given theirs once each is online as a
        // party bot (PastProfessions).
        void DecideProfessionsOfOnline();
        // Who is online: a companion back for ARRIVE_SETTLE_MS (its AI has looked at itself)
        // gets what it did while away (OfflineLife); last_seen is written every LAST_SEEN_MS.
        void UpdatePresence();
        OfflineLife::Result Arrive(Player* bot, PartyBotAI* ai, CompanionInfo& info);

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
        std::vector<uint32> m_pastQuestsPending;    // char guids, see DecidePastQuestsOfOnline
        std::vector<uint32> m_professionsPending;   // char guids, see DecideProfessionsOfOnline
        std::unordered_map<uint32, uint32> m_arrivingSinceMs;   // char guid -> m_nowMs first seen
        std::unordered_set<uint32> m_present;                   // char guids arrived this session
        uint32 m_presenceCheckMs = 0;
        uint32 m_lastSeenWriteMs = 0;
        float m_skillUpPriceFactor = 1.0f;          // Companion.SkillUp.PriceFactor
        mutable std::mutex m_affinityLock;
    };
}

#define sCompanionMgr Companions::CompanionMgr::Instance()

#endif
