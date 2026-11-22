/*
 * World population bot: a temporary character (never saved) that only adds atmosphere.
 * It refuses trades and duels, and group invites unless it is a roster member invited by a real
 * player (then it is recruited as a companion, Companions/CompanionMgr). Phase 1a behaviour is idle (emotes, sitting,
 * turning); city and field behaviours come in phases 1b / 1c.
 *
 * Threads: UpdateAI and OnPacketReceived run on the map thread. Settings come from
 * PopulationMgr atomics; nothing else of the manager is touched here.
 */

#ifndef MANGOS_POPULATION_BOT_AI_H
#define MANGOS_POPULATION_BOT_AI_H

#include "TeamBotAI.h"
#include "Population/PopulationTypes.h"
#include "Life/Errands/LootRun.h"
#include "Life/Errands/Trip/RoadWalk.h"
#include "Population/TownLife.h"
#include "Timer.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Population
{
    // A TeamBotAI: a field bot is a team of one the Commander fights with (Commander/Team.h,
    // unit SpawnRequest::unitId) and hunts for (HuntAssigner). City bots never fight: no team.
    class PopulationBotAI : public TeamBotAI
    {
    public:
        PopulationBotAI(SpawnRequest const& request, std::string name, std::shared_ptr<BotHandle> handle);
        ~PopulationBotAI() override;

        bool OnSessionLoaded(PlayerBotEntry* entry, WorldSession* sess) override;
        void OnPlayerLogin() override;
        void UpdateAI(uint32 const diff) override;
        void OnPacketReceived(WorldPacket const* packet) override;

        // No player leads it.
        Player* GetPartyLeader() const override { return nullptr; }
        // A road traveller fights only what attacks it (no HuntAssigner fights); so does someone
        // walking to or from a quest's creatures.
        bool SeeksFights() const override
        {
            return m_request.kind != BotKind::Traveller &&
                   (m_questStep == QuestStep::None || m_questStep == QuestStep::Hunt);
        }
        // Hunting for a quest: its creature first.
        uint32 PreferredPrey() const override
        {
            return m_questStep == QuestStep::Hunt ? m_request.quest.preyEntry : 0;
        }
        QuestStep GetQuestStep() const { return m_questStep; }
        uint32 GetQuestKills() const { return m_questKills; }

        SpawnRequest const& GetRequest() const { return m_request; }
        std::string const& GetBotName() const { return m_name; }
        bool IsInitialized() const { return m_initialized; }

        // Dev review of actions (.popbot emote, addon window /pbemote): clears the previous action,
        // faces the viewer, shows one action and holds still for REVIEW_HOLD_MS (no idle
        // behaviour) so it can be watched. Text emotes are sent as the bot's own /emote, at the viewer.
        enum class ReviewKind : uint8 { OneShot, State, Stand, Text, Sheath };
        static constexpr uint32 REVIEW_HOLD_MS = 5 * 60 * 1000;
        void PlayForReview(ReviewKind kind, uint32 id, Player* viewer);
        void EndReview();

        // The manager could not recruit it (world thread, maps idle): it declines the invite
        // soon and whispers the one who asked why (companion slots doc; TooHigh: above his
        // level, recruiting doc §1).
        enum class DeclineReason : uint8 { NoSlot, Farewell, TooHigh };
        void DeclineRecruit(DeclineReason reason);

        // Talking with a real player (Population/StrangerTalk, map thread). He stops what he
        // does and faces the player a while (TALK_HOLD_MS, or until a fight).
        static constexpr uint32 TALK_HOLD_MS = 30 * IN_MILLISECONDS;
        void HoldForTalk(Player* player);
        // He agreed to join `player`: the manager recruits him on its next update, as a trial
        // partner, or `bonded` as a companion at once (the first one, FirstCompanion).
        void Join(Player* player, bool bonded);
        // A roster member (or stranger): he can join someone; random test bots cannot.
        bool CanJoin() const { return m_request.rosterId != 0 && m_request.kind != BotKind::Test; }
        // His answer to one player asking him to join, rolled once while he is online.
        enum class JoinAnswer : uint8 { Unasked, Yes, No };
        JoinAnswer& AnswerTo(uint32 playerGuid) { return m_joinAnswers[playerGuid]; }
        std::vector<std::string> const& Tags() const { return m_request.tags; }

        // BotKind::Herald (the player's first companion): the player he came for, and his answer
        // on the offer page (StrangerTalk first:yes / first:no).
        bool IsHeraldFor(Player const* player) const;
        void HeraldAnswer(Player* player, bool yes);

        // Field bots: fights before it moves on (logs out out of sight).
        static constexpr uint32 FIELD_FIGHTS_MIN = 5;
        static constexpr uint32 FIELD_FIGHTS_MAX = 15;
        // Nothing to hunt for this long: moves on.
        static constexpr uint32 FIELD_IDLE_LEAVE_MS = 60 * 1000;
        // Travellers on foot who run (the others walk).
        static constexpr uint32 TRAVELLER_RUN_PCT = 90;
        // Mount casts failing in a row (indoors, ...) before it goes on foot.
        static constexpr uint32 MOUNT_TRIES = 3;
        // Acting out a quest: hunting this long at most, back to the area's middle after this
        // long without a fight, by the giver this long.
        static constexpr uint32 HUNT_GIVE_UP_MS = 12 * 60 * IN_MILLISECONDS;
        static constexpr uint32 HUNT_IDLE_WALK_MS = 40 * IN_MILLISECONDS;
        static constexpr uint32 AT_GIVER_MIN_MS = 8 * IN_MILLISECONDS;
        static constexpr uint32 AT_GIVER_MAX_MS = 20 * IN_MILLISECONDS;

    private:
        // Field bot tick: the Commander's body steps (TeamBotAI) around its own life: looting
        // after a fight (Life/Errands/LootRun.h), counting fights, moving on.
        // Travellers too (BotKind::Traveller): instead of hunting, walking the road (UpdateRoad).
        void UpdateField(uint32 diff);
        void InitializeField();
        // Traveller out of combat: on its mount again, along the road; at its end done.
        void UpdateRoad();
        // Field bot acting out a quest (SpawnRequest::quest): the steps, the kills of its
        // creature, the walks between giver and area.
        void BeginQuestStep(QuestStep step, float from);
        void CountQuestKills(bool inCombat);
        void AdvanceQuest();
        void UpdateQuestWalk();
        // From where it stands along the quest's way from `from` (yards from the giver), to the
        // area or back to the giver.
        std::vector<Commander::Roads::RoadPoint> QuestLine(bool towardsArea, float from) const;
        void Initialize();
        void EquipForTier();
        void HideFromNearbyPlayers();
        void UpdateSocial(uint32 diff);
        void UpdateIdle();
        void UpdateCrafter();
        // Standers, movers and sitters of a city or town live their day there (TownLife.h).
        bool LivesTownLife() const;
        void SetupProfession();
        void PickRecipe();
        void ClearCraftedItems();
        void MoveTo(float x, float y, float z);
        bool ReachedTarget() const;
        void FacePoint(float x, float y);
        void DeclineInviteNow();
        void Whisper(Player* to, std::string const& line);
        // Walk in from the entrance (logged in out of sight) / out to the leave spot.
        void UpdateEntering();
        void BeginLeaving();
        void UpdateLeaving();
        void ArriveAtSpot();
        void LogOut();

        SpawnRequest m_request;
        std::string m_name;
        std::shared_ptr<BotHandle> m_handle;

        // (m_updateTimer comes from TeamBotAI: the population interval in town, the
        // Commander's bot interval in the field.)
        uint32 m_nextIdleAtMs = 0;      // WorldTimer::getMSTime()
        uint32 m_reviewUntilMs = 0;     // != 0: held still for a review (PlayForReview)

        // Field
        ObjectGuid m_teamGuid;          // joined Commander::Team unit with this guid
        Life::LootRunState m_lootRun;
        bool m_wasInCombat = false;
        uint32 m_fights = 0;
        uint32 m_fightsToLeave = 0;

        // Traveller
        Life::Trip::RoadWalkState m_road;
        uint32 m_mountSpell = 0;        // rides this (0: on foot)
        uint32 m_mountTries = 0;        // failed mount casts in a row (indoors, ...): on foot after a few
        bool m_roadDone = false;

        // Quest (field bot; walks use m_road)
        QuestStep m_questStep = QuestStep::None;
        uint32 m_questStepSinceMs = 0;
        uint32 m_questKills = 0;
        uint32 m_questIdleSinceMs = 0;      // Hunt: no fight since
        uint32 m_questDoneAtMs = 0;         // AtGiver: handed in at
        std::vector<ObjectGuid> m_preyFought;

        // Coming in from out of sight, living at the spot, going out of sight to log out.
        enum class Phase : uint8 { Entering, Living, Leaving, Gone };
        Phase m_phase = Phase::Living;
        uint32 m_walkRetries = 0;
        // m_initialized comes from CombatBotBaseAI.

        // Movement target of the current MovePoint (walking in or out).
        float m_targetX = 0.0f, m_targetY = 0.0f, m_targetZ = 0.0f;
        uint32 m_moveDeadlineMs = 0;
        float m_homeX = 0.0f, m_homeY = 0.0f, m_homeZ = 0.0f;   // the spawn spot

        TownLife m_townLife;

        // Crafter: casts a real recipe in batches (reagents are added, products thrown away).
        uint32 m_professionSkill = 0;
        uint32 m_recipeSpell = 0;
        uint32 m_batchLeft = 0;
        uint32 m_craftRestUntilMs = 0;
        uint32 m_craftFailures = 0;

        // Pending refusals (ms countdowns, 0 = none).
        uint32 m_declineInviteInMs = 0;
        // The i18n key of what he says with the decline, always (DeclineRecruit); empty: the usual
        // chance of a "no thanks". Said in the reader's language (DeclineLine).
        std::string m_declineKey;
        std::string DeclineLine(Player* reader, std::string const& key = std::string()) const;
        // Who the decline line goes to when no invite is pending (he asked in the talk window).
        ObjectGuid m_declineTo;
        // A real player invited him: once the invite is declined, he asks why (StrangerTalk).
        ObjectGuid m_invitedBy;
        uint32 m_cancelTradeInMs = 0;

        // Talk (HoldForTalk): WorldTimer ms the hold ends, 0 = none.
        uint32 m_talkHoldUntilMs = 0;
        bool UpdateTalkHold();
        std::unordered_map<uint32, JoinAnswer> m_joinAnswers;

        // Herald: runs to the player, says why, offers to join (recruiting doc §3).
        enum class HeraldStep : uint8 { Approach, Speak, Waiting, Joined, Leaving };
        static constexpr uint32 HERALD_APPROACH_MS = 3 * 60 * 1000;    // gives up reaching him after this
        static constexpr float HERALD_TALK_YD = 4.0f;
        static constexpr uint32 HERALD_LINE_GAP_MS = 4000;
        static constexpr uint32 HERALD_ANSWER_MS = 90 * 1000;
        HeraldStep m_heraldStep = HeraldStep::Approach;
        uint32 m_heraldLine = 0;            // next line to say (Speak)
        uint32 m_heraldNextMs = 0;          // WorldTimer ms of the next line / move / give up
        uint32 m_heraldDeadlineMs = 0;
        void UpdateHerald();
        void HeraldLeave();
    };
}

#endif
