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
#include "Companions/Affinity/Traits.h"
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
        AskPlayer = 1,  // the player picks before affinity allows it (dev: .partybot rewardmode);
                        // from Gates PickRewards on it asks anyway (RewardChoice)
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
        // 0..MAX_AFFINITY: what the player did for it (design doc 2026-11-21-companion-affinity §2).
        // Through Get/AddAffinity only (map threads change it).
        uint32 affinity = 0;
        // Times it got affinity for money the player gave when it asked (AffinitySources). Under
        // the affinity lock.
        uint32 moneyGifts = 0;
        // Max-level dungeon and raid clears with the player, segment (Shared/DungeonClears) ->
        // times. Under the affinity lock.
        std::map<uint8, uint32> clears;
        // Unix time it was last seen online (OfflineLife counts from it), 0 = never. World thread.
        uint32 lastSeen = 0;
        // It logged out with its leader while told to wait (Commander Situation/Stay): it waits
        // there again when it comes back with him (TakeStay). World thread.
        bool staying = false;
        // Sent home this server run (SendHome); false again once it is online. World thread.
        bool home = false;
        // Levels it grew on the bench since it was last online (UpdateBench); said and cleared
        // when it arrives. World thread.
        uint32 benchLevels = 0;
        // The owner's active time it has been out on the bench since its last parcel
        // (Bench/Expedition); 0 while it is with him. World thread.
        uint32 expeditionSecs = 0;
        // A former companion (故人, slots doc §3): the player said goodbye. He takes no slot, is
        // not called over, does not grow on the bench nor mail parcels, until he is invited back
        // (Rehire). World thread.
        bool parted = false;
        // Temporary companions only (PretendAway): the offline skill points of the day, which a
        // real companion keeps in custom_companion_skillup.
        uint32 temporaryDay = 0;
        std::map<uint32, uint32> temporarySkillUps;
        // 搭伙 (a trial partner, recruiting doc §2.2): saved and in the party like a companion,
        // but not one yet: no affinity, no class quest asks, town trips nor money asks. Ends with
        // a promotion (Promote) or when he leaves the world (his character is deleted then, the
        // roster member lives on). Under the affinity lock while online (map threads change it).
        bool trial = false;
        uint32 trialQuests = 0;         // quests done with the player (Recruit/Trial)
        uint32 trialOfferAtMs = 0;      // WorldTimer ms he offers to stay, 0 = none due (runtime)
        // Friend traits (Affinity/Traits): rolled at affinity 80 / 90, then kept. None = not yet.
        // Through Get/SetTraits only (map threads set them).
        Traits::Trait traitLife = Traits::Trait::None;
        Traits::Trait traitCombat = Traits::Trait::None;
        // At affinity 100 he joined his player's account (account doc .kiro/docs/
        // 2026-11-21-companion-account.md): the character is the player's now, any character of
        // that account calls him and may log him in. No slot, no bench growth, parcels, home
        // trips nor offline life (the player may be playing him). World thread.
        bool inAccount = false;
        // He asked the player once to join the account (Account/JoinOffer); he does not ask
        // again, the companions panel and the talk window still offer it. World thread.
        bool joinAsked = false;
    };

    struct TraitPair
    {
        Traits::Trait life = Traits::Trait::None;
        Traits::Trait combat = Traits::Trait::None;
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
        // Recruited: a companion or a trial partner (both saved, both loot for themselves).
        bool IsCompanion(uint32 charGuid) const { return m_companions.find(charGuid) != m_companions.end(); }
        // A trial partner (CompanionInfo::trial).
        bool IsTrial(uint32 charGuid) const
        {
            CompanionInfo const* info = Find(charGuid);
            return info && info->trial;
        }
        // A companion and not a trial partner: affinity, class quests, town trips are his.
        bool IsBonded(uint32 charGuid) const
        {
            CompanionInfo const* info = Find(charGuid);
            return info && !info->trial;
        }
        CompanionInfo const* Find(uint32 charGuid) const;
        // World thread. Copies of everyone `recruiterGuid` recruited (temporary ones too), any order.
        std::vector<CompanionInfo> OfRecruiter(uint32 recruiterGuid) const;
        // Any thread. His companions (not trial partners nor temporary ones; former and account
        // ones too: everyone he ever bonded with and did not forget).
        uint32 BondedCountOf(uint32 recruiterGuid) const;
        // Any thread. Tells `player`'s addon that `bot` became his companion (trial promotion or
        // the first companion's yes): PBTRIAL;<bot>;bond;<first 1/0>, first = his only companion
        // (Recruit.lua note, Guide.lua welcome).
        void TellBond(Player* player, Player* bot) const;
        // Auto for anyone that is not a companion.
        RewardMode GetRewardMode(uint32 charGuid) const;
        // World thread (a command, later the affinity system); stored at once.
        void SetRewardMode(uint32 charGuid, RewardMode mode);

        // Any thread. Add: capped at MAX_AFFINITY, stored at once (not for temporary companions);
        // returns the new value (0: not a companion).
        uint32 GetAffinity(uint32 charGuid) const;
        uint32 AddAffinity(uint32 charGuid, uint32 points, char const* reason);
        // Any thread (dev console affinity tests). Sets it, capped at MAX_AFFINITY, stored at once
        // (not for temporary companions); returns the new value (0: not a companion).
        uint32 SetAffinity(uint32 charGuid, uint32 value);
        // Any thread. Its friend traits (Affinity/Traits); both None when not a companion.
        TraitPair GetTraits(uint32 charGuid) const;
        // Any thread. Gives it the trait of `tier` (None clears it), stored at once (not for
        // temporary companions).
        void SetTrait(uint32 charGuid, Traits::Tier tier, Traits::Trait trait);
        // Any thread. Counts one money gift if it had fewer than `max` (stored at once, not for
        // temporary companions); false: not a companion, or `max` reached.
        bool CountMoneyGift(uint32 charGuid, uint32 max);
        // Any thread. Counts one clear of `segment` (stored at once, not for temporary companions);
        // returns how many it has now (1 = the first), 0 = not a companion.
        uint32 CountClear(uint32 charGuid, uint8 segment);

        // Saves an online temporary bot as a real character and queues its login as a party bot
        // of the recruiter. The caller logs the temporary bot out afterwards. False: nothing
        // changed (also when the recruiter has no free slot: check HasFreeSlot first to say why).
        // `trial`: he joins as a trial partner (搭伙, no affinity yet), else as a companion.
        bool Recruit(Player* bot, Player* recruiter, uint32 memberId, bool trial);

        // Trial partners (recruiting doc §2.2, Recruit/Trial). Any thread (the player's map).
        // One more quest done with the player: how many now (0: not a trial partner).
        uint32 CountTrialQuest(uint32 charGuid);
        // A dungeon cleared together counts as all the quests: at least `quests` now.
        void RaiseTrialQuests(uint32 charGuid, uint32 quests);
        uint32 TrialQuests(uint32 charGuid) const;
        // He offers to stay at `atMs` (WorldTimer ms; an earlier one already due stays).
        void ScheduleTrialOffer(uint32 charGuid, uint32 atMs);
        // The offer is due now: true once (cleared).
        bool TakeTrialOffer(uint32 charGuid, uint32 nowMs);
        // He becomes a companion: affinity starts (AffinitySources::StartAt). Returns it, 0 =
        // not a trial partner (nothing changed).
        uint32 Promote(Player* bot);
        // A trial partner leaves the world unpromoted (PartyBotAI::OnLeaving): his character is
        // deleted once he is out (Update), the roster member lives on. World thread.
        void OnTrialLeave(uint32 charGuid);

        // The player's first companion came to him already (Recruit/FirstCompanion: once per
        // character, whatever he answered). World thread.
        bool FirstOffered(uint32 ownerGuid) const;
        void MarkFirstOffered(uint32 ownerGuid);

        // Slots (slots doc, Slots/Slots.h): his companions in the party and on the bench count,
        // former and temporary ones do not. World thread.
        struct SlotCount
        {
            uint32 used = 0;
            uint32 total = 0;           // free at his level + bought
            uint32 nextFreeLevel = 0;   // the level the next free one opens at, 0: all open
            bool canBuy = false;        // all free ones open, not at Companion.Slots.Max
            uint32 price = 0;           // copper of the next one to buy (canBuy)
        };
        SlotCount SlotsOf(Player const* owner) const;
        bool HasFreeSlot(Player const* owner) const;
        // He buys his next slot (pays SlotCount::price). "" = bought, else an error key:
        // notyet (free ones still to open), maxslots, nomoney.
        std::string BuySlot(Player* owner);

        // Former companions (故人, slots doc §3). World thread. "" = done, else an error key.
        // Part (道别): he becomes a former companion; online he says goodbye and leaves (as
        // PartyBotAI::Dismiss), offline he leaves the owner's group. notcompanion, notyours, former.
        std::string Part(Player* owner, uint32 charGuid);
        // A former companion is a companion again (on the bench; CallOver brings him).
        // notcompanion, notyours, notformer, noslot.
        std::string Rehire(Player* owner, uint32 charGuid);
        // Farewell for good (永别): his character is deleted. `memberId` (0: none) and `level`:
        // the roster member he was, to live on as a stranger to the owner (PopulationMgr::
        // ReturnToRoster). notcompanion, notyours, notformer, leaving (still in the world).
        std::string Forget(Player* owner, uint32 charGuid, uint32& memberId, uint32& level);

        // They come and go with the player (design doc 2026-11-21-companion-presence §2.1): a
        // companion whose leader logs out stays in his group as an offline member (PlayerBotMgr
        // keeps it there, PartyBotAI::OnLeaving); when he asks to enter the world again, the
        // companions of his group log in where they logged out and wait for him (PartyBotAI's
        // returning constructor). World thread (the login packet handler).
        void OnLoginRequest(uint32 charGuid);
        // A companion leaves the world: with its leader (he logged out; `staying`: it was told to
        // wait) or for good (kicked, removed). World thread.
        void OnLeave(uint32 charGuid, bool withLeader, bool staying);
        // It came back with its leader: was it waiting? Cleared once taken. World thread.
        bool TakeStay(uint32 charGuid);

        // The player calls a companion over (presence §2.3: the companions panel, `.partybot
        // call`; `.partybot load` of a companion too): it logs in where it is (its place, its
        // home), joins his group and comes to him (summoned or on its own, PartyBotAI::
        // UpdateTravel). `role`: CombatBotRoles, ROLE_INVALID = its own. `ownerOnly`: only his
        // own companions. World thread. "" = on its way, else an error key: notcompanion,
        // notyours, enemy (the other faction, SameSide), online, full, failed.
        std::string CallOver(Player* leader, uint32 charGuid, uint8 role, bool ownerOnly);
        // A group invite by name to a character that is not online (WorldSession::
        // HandleGroupInviteOpcode): one of his own companions is called over (CallOver) and
        // the addon told as from the panel (PBOK;<name>;callover, or PBERR;<name>;<full|failed|
        // online>). False: not one of his companions, the invite fails as usual. World thread.
        bool OnInviteOffline(Player* inviter, std::string const& name);

        // Account characters (account doc): the characters of the player's own account, a
        // companion who joined it at affinity 100 and the player's own ones. Any other character
        // of the account calls them over like companions (CallOver); they are saved, loot for
        // themselves and pay their own way (no money asks).
        //
        // `charGuid` is another character of `owner`'s account (owner a real player). Any thread
        // (the name cache).
        static bool IsOfAccount(Player const* owner, uint32 charGuid);
        // `charGuid` is of `leader`'s faction (race from the name cache, offline too). Companions and
        // account characters only ever come to their own side, whatever AllowTwoSide.Interaction.
        // Group says (that one is for real players); a game master in GM mode is let through.
        // Unknown character: false. Any thread.
        static bool SameSide(Player const* leader, uint32 charGuid);
        // The same for a bot in the world (a population bot asked to join, not in the name cache).
        static bool SameSide(Player const* leader, Player const* bot);
        // `owner` may do what a companion's owner does with him: his recruiter (a companion not in
        // an account), or any character of the account he is in (account characters). Any thread.
        bool MayManage(Player const* owner, uint32 charGuid) const;
        // A companion who joined his player's account (CompanionInfo::inAccount). Any thread.
        bool IsInAccount(uint32 charGuid) const
        {
            CompanionInfo const* info = Find(charGuid);
            return info && info->inAccount;
        }
        // The guid is in the world (or on its way) as a bot of a character of its own account:
        // saved like a companion, loots for itself. Marked by CallOver, `.partybot add` and the
        // return with the leader (UpdateReturns); forgotten when it logs out (PlayerBotMgr). Any
        // thread.
        bool IsAccountBot(uint32 charGuid) const;
        void MarkAccountBot(uint32 charGuid);
        void ForgetAccountBot(uint32 charGuid);
        // An account character playing as a bot (a former companion in an account, or the
        // player's own one): pays its own way, never asks the player for money. Any thread.
        bool PaysOwnWay(uint32 charGuid) const { return IsInAccount(charGuid) || IsAccountBot(charGuid); }

        // The other characters of `owner`'s account (not him) on his side (SameSide), by kind (the
        // companions panel). World thread.
        enum class AccountKind : uint8
        {
            Companion = 1,      // joined at affinity 100 (has a companion record)
            Own = 2,            // the player made him
        };
        struct AccountCharacter
        {
            uint32 charGuid = 0;
            AccountKind kind = AccountKind::Own;
        };
        std::vector<AccountCharacter> AccountCharactersOf(Player const* owner) const;

        // At affinity MAX_AFFINITY a companion joins `owner`'s account (account doc §2): the
        // character moves to it, nothing else changes (online he stays in the party). "" = done,
        // else an error key: notcompanion (also a trial partner), notyours, enemy, inaccount, locked
        // (affinity below MAX_AFFINITY), accountfull (CharactersPerRealm characters already).
        // World thread (the command, the talk window).
        std::string JoinAccount(Player* owner, uint32 charGuid);
        // He asked once (Account/JoinOffer, the answer or the window closed): not again.
        void MarkJoinAsked(uint32 charGuid);
        // Sends an account companion back to the companions account, as a companion of `owner`
        // (account doc §4): with a free slot he is one again (on the bench or with him), else
        // a former companion (故人; online he says goodbye). `parted`: which one. "" = done, else:
        // notcompanion (the player's own character: those are deleted at character select only),
        // notyours, enemy (an alt of the other faction), notinaccount, self (the character he plays).
        std::string SendBack(Player* owner, uint32 charGuid, bool& parted);

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
        // OnLoginRequest's companions: logged in at their own position, until RETURN_WINDOW_MS.
        void UpdateReturns();
        // Companions out of the world and of any group, away for HOME_AFTER_SEC: their saved
        // position becomes their home (character_homebind, the city they were recruited in), as
        // if they had gone back there (presence §2.2). Every HOME_CHECK_MS.
        void SendHome();
        uint32 m_homeCheckMs = 0;
        // Companions on the bench (out of the world, not in their owner's group), while their
        // owner is online (bench doc): they grow with the experience he gains (Bench/
        // BenchLeveling, written straight into their saved character) and mail him a parcel
        // every so often of his active time (Bench/Expedition). Every BENCH_CHECK_MS.
        void UpdateBench();
        uint32 m_benchCheckMs = 0;
        // Owner char guid -> his total experience at the last look (custom_companion_owner).
        std::unordered_map<uint32, uint32> m_ownerXpSeen;
        // Owner char guid -> slots he bought (custom_companion_owner.slots_bought).
        std::unordered_map<uint32, uint32> m_slotsBought;
        // Only his first PARCEL_SENDERS benched companions (by recruit time) mail parcels (slots
        // doc §2.3: dozens of bought slots must not flood his mailbox nor his purse).
        static constexpr uint32 PARCEL_SENDERS = 4;
        // `info`'s place among its owner's benched companions by recruit time, 0 = the first.
        uint32 BenchRank(Player const* owner, CompanionInfo const& info) const;
        // A benched companion as saved in `characters`, read once and kept while it stays out
        // (forgotten once it is online).
        struct Benched
        {
            uint32 level = 1;
            uint32 xp = 0;
            uint32 zone = 0;
        };
        std::unordered_map<uint32, Benched> m_benched;
        // Benched::level etc. of `info`, read from `characters` the first time; null: not there.
        Benched* BenchedOf(CompanionInfo const& info);
        void SendParcel(Player* owner, CompanionInfo const& info, Benched const& benched, uint32 rank);
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
        // Trial partners that left unpromoted (OnTrialLeave, or found at startup): once out of
        // the world, their characters are deleted and the roster members live on (UpdateTrialEnds).
        std::vector<uint32> m_trialEnds;
        std::unordered_set<uint32> m_firstOffered;      // owner char guids (custom_companion_owner.first_offered)
        void UpdateTrialEnds();
        // His character and every companion row of him go (Forget, a trial that ended).
        void DeleteCharacter(uint32 charGuid);

        struct PendingLogin
        {
            uint32 charGuid = 0;
            ObjectGuid recruiter;
            uint32 sinceMs = 0;
        };

        // A player entering the world (OnLoginRequest): the offline companions of his group follow.
        struct Return
        {
            ObjectGuid leader;
            uint32 sinceMs = 0;
            std::unordered_set<uint32> sent;    // char guids logged in for him already
        };

        // Sets the character's account (`characters`, the name cache, the online bot's save
        // target, both accounts' character counts). World thread.
        void MoveToAccount(uint32 charGuid, uint32 fromAccount, uint32 toAccount);
        // A player logs in a character that is still an offline member of the party of another
        // character of his account (it was his bot there): it leaves that party, he starts on
        // his own (account doc §3).
        void LeaveAltsParty(uint32 charGuid);

        std::unordered_set<uint32> m_accountBots;   // char guids (IsAccountBot)
        mutable std::mutex m_accountBotsLock;

        std::atomic<bool> m_enabled{ true };
        std::vector<Return> m_returns;
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
