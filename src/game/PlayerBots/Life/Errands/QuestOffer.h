/*
 * Bot daily life (PlayerBots/Life), errand: a quest the leader cannot take (user 2026-11-22). The
 * leader stands by a giver (GiverVisit::LeaderBy) that gives a quest the bot may take and he has
 * not in his log:
 *  - he handed it in, or the rules keep him from it (BotShared::QuestRules::Barred: level, class,
 *    race, chain, skill, reputation): the bot asks him whether it should take it;
 *  - he could take it (nothing, or only a full log / full bags): nothing; the bot takes it when
 *    he does (PlayerBotMgr::OnPlayerAcceptedQuest, QuestPickUp::AddLeaderQuests).
 * And the other way round (user 2026-11-22): a quest of that giver the leader has or handed in
 * that the bot has not done and cannot take yet for a reason it would see in game (too low, log
 * full, bags full) is told to the leader once per TOLD_MS (TakeBlocked; the caller says it).
 * Not offered: quests a bot cannot do along with the player (event or exploration objectives such
 * as escorts, timed quests, spell objectives) and mirror quests.
 *
 * One question for the party (Desk, like the mage's water in ProvisionDesk.h): the bots wanting
 * quests at that giver queue up; after GATHER_MS the first asks for all of them (their names and
 * quests in the text). Bots that come while it is open wait for the next one. Yes: each named bot
 * takes its quests (Approved, then QuestPickUp takes them like the leader's quests: at the giver
 * with him, no accept scripts). No: those quests are not offered again while the bot stays with
 * this leader (cleared at the bot's login). No answer: not for QUIET_MS. All bots gone from the
 * queue (the leader walked off, a fight): the question is withdrawn.
 *
 * Design: .kiro/docs/2026-11-22-companion-quest-interaction.md (workspace root). The desk: any
 * thread (one mutex). The errand: the bot's map thread.
 */

#ifndef MANGOS_LIFE_QUEST_OFFER_H
#define MANGOS_LIFE_QUEST_OFFER_H

#include "Common.h"
#include "ObjectGuid.h"
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

class Player;
class Quest;
class WorldObject;

namespace Life
{
namespace QuestOffer
{
    enum class Answer : uint8
    {
        Wait,       // queued, or the question is open
        Yes,
        No,
        Silent,     // no answer
    };

    struct Reply
    {
        Answer answer = Answer::Wait;
        std::vector<uint32> quests;     // what it was asked about (Yes / No / Silent)
    };

    class Desk
    {
    public:
        static Desk& Instance();

        static constexpr uint32 GATHER_MS = 2 * IN_MILLISECONDS;
        // A queued bot that has not called Want for this long left the queue.
        static constexpr uint32 SEEN_MS = 10 * IN_MILLISECONDS;
        // An answer waits this long for its bot to read it.
        static constexpr uint32 RESULT_KEEP_MS = MINUTE * IN_MILLISECONDS;

        // `bot` would take `quests` from `giver` (the leader stands by it). `tags`: the bot's
        // personality. Reads its answer once there is one.
        Reply Want(Player* bot, Player* leader, ObjectGuid giver, std::vector<uint32> const& quests,
                   std::vector<std::string> const& tags);
        // The bot wants nothing here any more (the leader walked off, a fight, it logged out).
        void Leave(ObjectGuid leader, ObjectGuid bot);

    private:
        Desk() = default;

        struct Asker
        {
            ObjectGuid bot;
            std::vector<uint32> quests;
            uint32 seenMs = 0;
        };

        struct LeaderQueue
        {
            ObjectGuid giver;
            std::deque<Asker> bots;         // the first one asks
            uint32 firstMs = 0;
            uint32 requestId = 0;
            std::map<ObjectGuid, std::pair<Reply, uint32>> results;     // bot -> answer, when
        };

        // With m_lock held. Reads the open question's answer into `results`.
        void Settle(LeaderQueue& q, uint32 now);
        void Tidy(uint32 now);

        mutable std::mutex m_lock;
        std::map<ObjectGuid, LeaderQueue> m_leaders;
    };

    // A quest the leader has or handed in that the bot cannot take yet for a reason it would see
    // in game (not class, race, chain: those quests it would not even see).
    enum class Block : uint8
    {
        Level,      // too low (`level`: the quest's minimum)
        LogFull,
        BagsFull,   // no room for the item the quest gives
    };

    struct BlockedQuest
    {
        uint32 questId = 0;
        Block block = Block::Level;
        uint32 level = 0;
        ObjectGuid giver;
    };

    // Per bot (a member of its AI). Plain data.
    struct QuestOfferState
    {
        std::vector<BlockedQuest> blocked;      // found this look, for the caller to tell (TakeBlocked)
        std::map<uint32, uint32> toldUntilMs;   // told: not again before
        ObjectGuid leader;              // whose answers these are (another leader: forgotten)
        ObjectGuid queuedAt;            // the leader whose queue it is in
        std::set<uint32> approved;      // he said yes: QuestPickUp takes them with him
        std::set<uint32> declined;      // he said no: not again while with him
        std::map<uint32, uint32> quietUntilMs;  // no answer: not before
        uint32 nextLookMs = 0;
    };

    constexpr float SEARCH_YD = 30.0f;
    constexpr uint32 LOOK_MS = 2000;
    constexpr uint32 QUIET_MS = 10 * MINUTE * IN_MILLISECONDS;
    constexpr uint32 TOLD_MS = 10 * MINUTE * IN_MILLISECONDS;

    // A quest a bot may do along with the player (see above).
    bool Offerable(Quest const* quest);

    // One tick (out of combat, with the errands). `tags`: the bot's personality.
    void Update(Player* me, QuestOfferState& state, Player* leader, std::vector<std::string> const& tags);
    // Out of the queue (a fight, staying behind); answers are kept.
    void Stop(Player* me, QuestOfferState& state);
    // The bot logged in: a new session with the leader.
    void Reset(QuestOfferState& state);
    // The approved quests it does not have yet (for QuestPickUp, as the leader's).
    std::vector<uint32> Approved(Player const* me, QuestOfferState const& state);
    // The quests it found it cannot take yet (each once per TOLD_MS), for the caller to say.
    std::vector<BlockedQuest> TakeBlocked(QuestOfferState& state);
}
}

#define sQuestOfferDesk Life::QuestOffer::Desk::Instance()

#endif
