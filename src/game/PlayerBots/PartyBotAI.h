/*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

/*
 * A member of a real player's party (TeamBotAI, the Commander's body): follows the leader,
 * takes the player's commands (.partybot), and a recruited companion's errands (Life/).
 */

#ifndef MANGOS_PARTYBOTAI_H
#define MANGOS_PARTYBOTAI_H

#include "TeamBotAI.h"
#include "Commander/Behaviors/Movement/FollowSpot.h"
#include "Commander/Behaviors/Movement/HumanFollow.h"
#include "Life/Errands/CraftRun.h"
#include "Life/Errands/LootRun.h"
#include "Life/Errands/QuestObjectRun.h"
#include "Life/Errands/QuestTurnIn.h"
#include "Life/Errands/NpcVisit.h"
#include "Life/Errands/MerchantTasks.h"
#include "Life/Errands/TownTrip.h"
#include "Life/Errands/Trip/ShoppingPlanner.h"
#include "Life/Errands/Trip/TrainingPlanner.h"
#include "Companions/RewardChoice.h"
#include "Companions/ClassQuests/MirrorSync.h"
#include "Companions/ClassQuests/ClassQuestFlow.h"
#include "Life/Errands/QuestPickUp.h"
#include "Life/Errands/QuestItemUse.h"
#include "Life/Errands/GiveProvisions.h"
#include "Life/Errands/AskProvisions.h"
#include "Shared/Trade.h"
#include <array>
#include <string>

class PartyBotAI : public TeamBotAI
{
public:

    PartyBotAI(Player* pLeader, Player* pClone, CombatBotRoles role, uint8 race, uint8 class_, uint8 level, uint32 mapId, uint32 instanceId, float x, float y, float z, float o)
        : TeamBotAI(), m_race(race), m_class(class_), m_level(level), m_mapId(mapId), m_instanceId(instanceId), m_x(x), m_y(y), m_z(z), m_o(o)
    {
        m_role = role;
        m_roleLocked = role != ROLE_INVALID;
        m_leaderGuid = pLeader->GetObjectGuid();
        m_cloneGuid = pClone ? pClone->GetObjectGuid() : ObjectGuid();
    }
    PartyBotAI(Player* pLeader, uint32 mapId, uint32 instanceId, float x, float y, float z, float o)
        : TeamBotAI(), m_mapId(mapId), m_instanceId(instanceId), m_x(x), m_y(y), m_z(z), m_o(o)
    {
        m_role = ROLE_INVALID;
        m_leaderGuid = pLeader->GetObjectGuid();
    }

    bool OnSessionLoaded(PlayerBotEntry* entry, WorldSession* sess) final;
    void OnPlayerLogin() final;
    void UpdateAI(uint32 const diff) final;
    void OnPacketReceived(WorldPacket const* packet) final;

    void CloneFromPlayer(Player const* pPlayer);
    void AddToPlayerGroup();

    // The player who spawned the bot, while bot and player are in the same group and the bot
    // does not lead it.
    Player* GetPartyLeader() const override;
    // Far behind the anchor it is teleported to (CatchesUpByTeleport), not walking back to it.
    bool FillsUpAtOnce() override;

    // A recruited companion (Companions/CompanionMgr): loots corpses itself, keeps its round
    // robin turns and rolls need / greed like a player. Other party bots leave loot to the player.
    bool LootsForSelf() const;
    uint8 ChooseLootRoll(uint32 itemId) const override;
    // Companions buy their reagents and ammo (Life/Errands/MerchantTasks.h); other bots get them.
    bool GetsFreeSupplies() const override;
    // Companions earn class quest abilities (Companions/ClassQuests); other bots get them.
    bool GetsFreeAbilities() const override;
    // Ends every daily-life errand walk (combat, a Commander plan, not a companion).
    void StopErrands();
    // Conjured supplies out of combat (user 2026-11-20): every party bot gives what it makes
    // (Life/Errands/GiveProvisions: the leader, bots that ask); a companion, which buys its
    // food, asks a mage of the party first (AskProvisions). True: busy, the bot stays put.
    bool UpdateProvisions(Player* pLeader);
    // Combat, a Commander plan, a resurrection first: their trades and casts stop for now.
    void PauseProvisions();
    // Out of combat, far from its anchor: teleported there (UpdateAI; and StartResting fills up
    // at once, it is about to be teleported). Party bots beyond PB_TELEPORT_TO_LEADER_YD;
    // companions only on another map (their town trips and the leader's own business keep them
    // apart on purpose: they walk, Life/Errands/TownTrip.h).
    bool CatchesUpByTeleport(Player const* pAnchor) const;
    static constexpr float PB_TELEPORT_TO_LEADER_YD = 100.0f;
    // A companion's personality tag keys (what it says, Life/Requests/Phrases.h); empty otherwise.
    std::vector<std::string> const& PersonalityTags() const;
    // Follow the leader (UpdateAI): the bot's own offset and style (Commander/Behaviors/Movement/
    // HumanFollow.h), moved out of idle creatures' aggro range (FollowSpot.h); on a transport
    // the original random offset.
    // `why`: the caller, kept in m_followEvents (dev console `ai().follow.events`).
    void StartFollowing(Unit* pLeader, char const* why);
    // While following: re-check the follow point after the leader moved or its heading turned enough.
    void KeepFollowSpotSafe(Unit* pLeader);
    bool UsesSafeFollow() const;
    Commander::FollowSpot::Offset ChooseFollowOffset(Unit* pLeader, float heading);
    void IssueFollow(Unit* pLeader, float heading, char const* why);
    // Every update with a leader: HumanFollow::Tick (drift, idle look / step / sit).
    void UpdateHumanFollow(Unit* pLeader, bool follows);
    // A dead leader (a body, or a ghost on its corpse run): stops the follow and the trail walk
    // and returns true, so the bot waits where it is (by the corpse) instead of going after him.
    bool WaitForDeadLeader(Player* pLeader);
    // Who this bot follows out of combat: the party's guide bot when one leads
    // (Commander/Coordination/Situation/Guide.h), else the leader. Null: this bot is the guide.
    Player* GetFollowTarget(Player* pLeader);
    // Who this bot stays near (teleport beyond 100 yd, the walk back after a corpse run): the
    // follow target, the leader for the guide itself.
    Player* FollowAnchor(Player* pLeader);
    // The guide: stops its own follow (nothing else), so it stands where it is walked to.
    void StandAsGuide();

    ObjectGuid m_leaderGuid;
    ObjectGuid m_cloneGuid;
    uint8 m_race = 0;
    uint8 m_class = 0;
    uint8 m_level = 0;
    uint32 m_mapId = 0;
    uint32 m_instanceId = 0;
    float m_x = 0.0f;
    float m_y = 0.0f;
    float m_z = 0.0f;
    float m_o = 0.0f;
    // The quest of this bot the leader follows in its quest log (Companions/ClassQuests/MirrorSync.h).
    // Set by the class quest flow (and `.partybot mirror follow`).
    Companions::MirrorSync::Link m_mirrorLink;
    // A companion's class quest asks (Companions/ClassQuests/ClassQuestFlow.h).
    Companions::ClassQuestFlow::State m_classQuestFlow;
    // Out of combat: takes the class quest steps the flow wants from their givers near the leader
    // (Life/Errands/QuestPickUp.h).
    Life::QuestPickUpState m_questPickUp;
    // Out of combat: a quest item used on the creature it is for (taming rods; Life/Errands/QuestItemUse.h).
    Life::QuestItemUseState m_questItemUse;
    // Resurrection before eating/drinking (UpdateAI): the last corpse tried, and since when the
    // bot has been trying without a success (per bot, whatever the corpse; empty guid = no window).
    // Longer than a 10 s Resurrection cast, so one slow cast that lands is not cut off.
    static constexpr uint32 PB_RESURRECT_TRY_MS = 15 * IN_MILLISECONDS;
    static constexpr uint32 PB_RESURRECT_BACKOFF_MS = 60 * IN_MILLISECONDS;
    ObjectGuid m_resurrectTryGuid;
    uint32 m_resurrectTryMs = 0;
    // Follow offset given to MoveFollow, and the leader position / heading of its last safety check.
    Commander::FollowSpot::Offset m_followOffset;
    float m_followCheckX = 0.0f;
    float m_followCheckY = 0.0f;
    float m_followCheckO = 0.0f;
    bool m_followChecked = false;
    // Who the last MoveFollow follows (IssueFollow): a new guide restarts the follow.
    ObjectGuid m_followTargetGuid;
    // The last MoveFollow calls (IssueFollow), for the dev console: who started the follow and
    // what movement it replaced (a follow restarted every few ticks: something else stops it).
    struct FollowEvent
    {
        uint32 ms = 0;
        char const* why = "";       // a string literal of the caller
        uint8 prevMotion = 0;       // MovementGeneratorType before the MoveFollow
        std::string owner;          // BotDirectives movement owner at that moment, if any
    };
    static constexpr size_t PB_FOLLOW_EVENTS = 16;
    std::array<FollowEvent, PB_FOLLOW_EVENTS> m_followEvents;
    size_t m_followEventNext = 0;
    uint32 m_followIssues = 0;
    Commander::HumanFollow::IdleState m_humanIdle;
    // Out of combat: the walk to corpses of a bot that loots for itself (Life/Errands/LootRun.h).
    Life::LootRunState m_lootRun;
    // Out of combat: the quest objects a companion's own quests need (Life/Errands/QuestObjectRun.h).
    Life::QuestObjectRunState m_questObjectRun;
    // Out of combat: a companion turns in its finished quests at a giver near the leader
    // (Life/Errands/QuestTurnIn.h); the reward is picked by m_rewardChooser.
    Life::QuestTurnInState m_questTurnIn;
    Companions::CompanionRewardChooser m_rewardChooser;
    // Out of combat: a companion's business at vendors and its class trainer near the leader
    // (Life/Errands/NpcVisit.h, MerchantTasks.h).
    Life::NpcVisitState m_vendorVisit;
    Life::VendorTask m_vendorTask;
    Life::NpcVisitState m_trainerVisit;
    Life::TrainerTask m_trainerTask;
    // Farther than the NPC visits, with the leader's consent (Life/Errands/TownTrip.h): the
    // auction house, mailbox and vendors, and the class trainer (m_trainerTask, so TakeLearned
    // sees what the trip learned).
    Life::TownTripState m_townTrip;
    Life::Trip::ShoppingPlanner m_shoppingPlanner;
    Life::Trip::TrainingPlanner m_trainingPlanner{ m_trainerTask };
    // Out of combat: a companion makes its conjured food and water, poison vials
    // (Life/Errands/CraftRun.h).
    Life::CraftRunState m_craftRun;
    // Conjured supplies (UpdateProvisions). One trade at a time (Shared/Trade.h): while its
    // hands trade, OnPacketReceived keeps the trade status from the default handling.
    BotShared::Trade::Hands m_tradeHands;
    Life::GiveProvisionsState m_giveProvisions;
    Life::AskProvisionsState m_askProvisions;
};

#endif
