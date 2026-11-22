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
#include "Life/Errands/CraftForPlayer.h"
#include "Life/Errands/LootRun.h"
#include "Life/Errands/GatherRun.h"
#include "Life/Errands/QuestObjectRun.h"
#include "Life/Errands/QuestTurnIn.h"
#include "Life/Errands/NpcVisit.h"
#include "Life/Errands/MerchantTasks.h"
#include "Life/Errands/TownTrip.h"
#include "Life/Errands/Trip/ShoppingPlanner.h"
#include "Life/Errands/Trip/TrainingPlanner.h"
#include "Life/Errands/Trip/ProfessionPlanner.h"
#include "Companions/RewardChoice.h"
#include "Companions/ClassQuests/MirrorSync.h"
#include "Companions/ClassQuests/ClassQuestFlow.h"
#include "Companions/ArtisanTrip.h"
#include "Companions/Affinity/AffinitySources.h"
#include "Companions/Affinity/TraitEffects.h"
#include "Companions/Recruit/Trial.h"
#include "Companions/Account/JoinOffer.h"
#include "Life/Errands/QuestPickUp.h"
#include "Life/Errands/QuestItemUse.h"
#include "Life/Errands/GiveProvisions.h"
#include "Life/Errands/AskProvisions.h"
#include "Life/Errands/LessonReminder.h"
#include "Life/Pastimes/WaitAround.h"
#include "Companions/Travel/Departure.h"
#include "Companions/Travel/HearthAlong.h"
#include "Companions/Travel/SummonOffer.h"
#include "Life/Errands/Travel.h"
#include "Life/Errands/WalkAway.h"
#include "Companions/Travel/HomeBind.h"
#include "Shared/Trade.h"
#include <array>
#include <string>

namespace Commander { struct StaySpot; }

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
    // A companion coming back with its leader (Companions/CompanionMgr::OnLoginRequest): it
    // logs in where it logged out, still a member of his group, and waits up to
    // PB_WAIT_LEADER_MS for him to enter the world.
    explicit PartyBotAI(ObjectGuid leader)
        : TeamBotAI(), m_atSavedPosition(true)
    {
        m_role = ROLE_INVALID;
        m_leaderGuid = leader;
    }

    bool OnSessionLoaded(PlayerBotEntry* entry, WorldSession* sess) final;
    void OnPlayerLogin() final;
    void UpdateAI(uint32 const diff) final;
    void OnPacketReceived(WorldPacket const* packet) final;

    void CloneFromPlayer(Player const* pPlayer);
    void AddToPlayerGroup();
    // Made the group's leader by the player (it leads nobody, GetPartyLeader would send it
    // away): the lead goes back to him, with a word.
    void HandBackLead();
    // It is about to log out (PlayerBotMgr's removal). True: a companion whose leader logged out,
    // it stays in his group and comes back with him (whether it was waiting somewhere is kept,
    // CompanionMgr::OnLeave); false: it leaves the group.
    bool OnLeaving();
    static constexpr uint32 PB_WAIT_LEADER_MS = 90 * IN_MILLISECONDS;

    // The player who spawned the bot, while bot and player are in the same group and the bot
    // does not lead it.
    Player* GetPartyLeader() const override;
    // Far behind the anchor (FarBehind): it travels there, not sitting down first.
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
    // Too far from its anchor to walk: another map, or beyond Summon::FAR_YD and out of his
    // sight (closer it walks: a town trip, the leader at his own trainer, Life/Errands/TownTrip.h).
    bool FarBehind(Player const* pAnchor) const;
    // A party bot far behind its anchor (FarBehind), out of combat: a warlock of the team will
    // summon it (GatherPlanner::SummonComing: it waits, up to PB_SUMMON_WAIT_MS), else it
    // travels there on its own (Life/Errands/Travel) and walks the last stretch (Arrival).
    // Companions and temporary bots alike: no party bot is teleported to the leader.
    // True: busy, nothing else this tick.
    bool UpdateTravel(Player* pAnchor);
    // Out of the party while the player who led it sees it (kicked, `.partybot remove`, he left
    // the group): a companion says goodbye and leaves by hearthstone or on foot out of his
    // sight (Life/Errands/WalkAway) before it logs out. True: still leaving. Tried once.
    bool UpdateWalkAway();
    Life::WalkAwayState m_walkAway;
    bool m_walkAwayTried = false;
    // `.partybot remove`: a companion leaves the group (and then walks away), others log out.
    void Dismiss();
    // A companion binds its hearthstone where the leader binds his (Companions/Travel/HomeBind).
    Companions::HomeBind::State m_homeBind;
    static constexpr uint32 PB_SUMMON_WAIT_MS = 3 * MINUTE * IN_MILLISECONDS;
    Life::TravelState m_travel;
    uint32 m_farSinceMs = 0;
    // A companion's personality tag keys (what it says, Life/Requests/Phrases.h); empty otherwise.
    std::vector<std::string> const& PersonalityTags() const;
    // His joining the account (the talk window's naming page kneels him: JoinOffer::Choose).
    Companions::JoinOffer::State& JoinOfferState() { return m_joinOffer; }

    // A companion's friend traits (Companions/Affinity/TraitEffects.h): the PlayerAI hooks core
    // code asks while this AI plays the character. Others have no traits (no change).
    Companions::TraitEffects::State m_traitEffects;
    void ChangeDamageTaken(Unit* attacker, uint32& damage, SpellEntry const* spell) override;
    void ChangeHealingTaken(uint32& heal) override;
    void ChangeAuraDuration(SpellEntry const* spell, bool positive, int32& duration) override;
    void ChangeResurrection(uint32& health, uint32& mana) override;
    float RegenRate() const override;
    float CastTimeRate(SpellEntry const* spell) const override;
    float PushbackRate() const override;
    float LockoutRate() const override;
    float FallDamageRate() const override;
    float BreathRate() const override;
    float SwimSpeedRate() const override;
    float ReputationRate() const override;
    float RepairCostRate() const override;
    float SellPriceRate(ItemPrototype const* proto) const override;
    float BuyPriceRate(ItemPrototype const* proto) const override;
    float LootMoneyRate() const override;
    float DurabilityLossRate() const override;
    float DetectedRangeRate() const override;
    int32 StealthDetectBonus() const override;
    float CraftExtraChance(SpellEntry const* spell) const override;
    uint32 SavedReagent(SpellEntry const* spell) override;
    float GatherExtraChance() const override;
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
    // Every update with a leader: HumanFollow::Tick (a new place at a stop, drift, idle look /
    // step / sit).
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
    // Told to wait here (Commander/Coordination/Situation/Stay.h): no follow; out of a fight it
    // walks back to its spot and passes the time there (Life/Pastimes/WaitAround.h). `free`:
    // nothing else has the bot now (no fight, no rest, no Commander plan or hold).
    void HoldStay(Commander::StaySpot const& spot, bool free);
    static constexpr float PB_STAY_BACK_YD = 4.0f;    // farther from its spot: walks back
    // The leader went through a party portal (a mage's, Shared/Portals) within PB_PORTAL_FOLLOW_YD:
    // it walks to the portal and goes through after him (else it would walk or teleport there).
    // True while it does.
    bool FollowThroughPortal(Player* pLeader);
    // Its part in a summoning ritual of the team (Commander/Coordination/Situation/Summon.h): the
    // warlock casts and channels on the target, a helper walks to the portal and clicks it, and
    // both keep still. Before UpdateBody (the warlock's channel must be watched). True: busy.
    bool UpdateSummon();
    struct SummonHands
    {
        ObjectGuid target;          // the ritual these hands are for
        bool cast = false;          // the warlock started the ritual
        uint32 castMs = 0;
        uint32 sentMs = 0;          // its channel ended (the summon went out)
    };
    SummonHands m_summonHands;
    // A summon confirmation came (SMSG_SUMMON_REQUEST): accepted at the next update out of combat.
    bool m_summonOffered = false;
    // The leader leaves by hearthstone or flight path: "shall we wait here?" (one bot asks,
    // Companions/Travel/Departure.h); a waiting warlock offers to summon him back (SummonOffer.h).
    Companions::Departure::State m_departure;
    // He hearths, ours bound at the same inn: it hearths with him (Companions/Travel/HearthAlong.h).
    Companions::HearthAlong::State m_hearthAlong;
    Companions::SummonOffer::State m_summonOffer;
    static constexpr uint32 PB_SUMMON_HELPERS_MS = 60 * IN_MILLISECONDS;   // nobody clicked: it gives up
    static constexpr uint32 PB_SUMMON_ACCEPT_MS = 125 * IN_MILLISECONDS;   // the confirmation's 2 minutes
    static constexpr float PB_SUMMON_ARRIVED_YD = 30.0f;
    static constexpr float PB_PORTAL_FOLLOW_YD = 40.0f;

    ObjectGuid m_leaderGuid;
    ObjectGuid m_cloneGuid;
    // Came back with its leader (the returning constructor): not teleported to him at login;
    // waits for him since m_waitLeaderSinceMs; m_restoreStay: it waits where it logged out again.
    bool m_atSavedPosition = false;
    uint32 m_waitLeaderSinceMs = 0;
    bool m_restoreStay = false;
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
    // A companion asks to be taken to an Artisan trainer (Companions/ArtisanTrip.h); near one it
    // walks over and learns (m_artisanTask through NpcVisit).
    Companions::ArtisanTrip::State m_artisanTrip;
    // A companion's affinity from levels and money gifts (Companions/Affinity/AffinitySources.h).
    Companions::AffinitySources::State m_affinitySources;
    // A trial partner (搭伙, Companions/Recruit/Trial.h): says the deal, offers to stay when due.
    Companions::Trial::State m_trial;
    // At affinity 100 he asks once to join the player's account (Companions/Account/JoinOffer.h).
    Companions::JoinOffer::State m_joinOffer;
    Life::ArtisanTask m_artisanTask;
    Life::NpcVisitState m_artisanVisit;
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
    // Waiting at its stay spot (HoldStay).
    Life::Pastimes::WaitAroundState m_waitAround;
    // Out of combat: the walk to corpses of a bot that loots for itself (Life/Errands/LootRun.h).
    Life::LootRunState m_lootRun;
    // Out of combat: a companion skins corpses and opens herb and ore nodes (Life/Errands/GatherRun.h).
    Life::GatherRunState m_gatherRun;
    // Out of combat: the quest objects a companion's own quests need (Life/Errands/QuestObjectRun.h).
    Life::QuestObjectRunState m_questObjectRun;
    // Out of combat: a companion turns in its finished quests at a giver near the leader
    // (Life/Errands/QuestTurnIn.h); the reward is picked by m_rewardChooser.
    Life::QuestTurnInState m_questTurnIn;
    Companions::CompanionRewardChooser m_rewardChooser;
    // What the trainer business knows of this bot (Life/Errands/Trip/LessonPriority.h: its role,
    // the spells its AI casts), refreshed by RefreshLessonContext when its spells or role change.
    Life::Lessons::Context m_lessonContext;
    // (spells are rebuilt from several places: refreshed every PB_LESSON_CONTEXT_MS and on a
    // role change)
    static constexpr uint32 PB_LESSON_CONTEXT_MS = 5 * IN_MILLISECONDS;
    uint32 m_lessonContextMs = 0;
    CombatBotRoles m_lessonContextRole = ROLE_INVALID;
    void RefreshLessonContext();
    // Out of combat: a companion's business at vendors and its class trainer near the leader
    // (Life/Errands/NpcVisit.h, MerchantTasks.h).
    Life::NpcVisitState m_vendorVisit;
    Life::VendorTask m_vendorTask{ &m_lessonContext };
    Life::NpcVisitState m_trainerVisit;
    Life::TrainerTask m_trainerTask{ &m_lessonContext };
    // Out in the field: important lessons waiting at the trainer (Life/Errands/LessonReminder.h).
    Life::LessonReminderState m_lessonReminder;
    // Farther than the NPC visits, with the leader's consent (Life/Errands/TownTrip.h): the
    // auction house, mailbox and vendors, and the class trainer (m_trainerTask, so TakeLearned
    // sees what the trip learned).
    Life::TownTripState m_townTrip;
    Life::Trip::ShoppingPlanner m_shoppingPlanner;
    Life::Trip::TrainingPlanner m_trainingPlanner{ m_trainerTask };
    // Profession trainers: the lowest business, last on the trip (Trip/ProfessionPlanner.h).
    Life::Trip::ProfessionPlanner m_professionPlanner{ &m_lessonContext };
    // Out of combat: a companion makes its conjured food and water, poison vials
    // (Life/Errands/CraftRun.h).
    Life::CraftRunState m_craftRun;
    // Out of combat: making something for a player with his materials, asked in the talk window
    // (Life/Errands/CraftForPlayer.h; any party bot, a companion from affinity 30).
    Life::CraftForPlayerState m_craftForPlayer;
    // Conjured supplies (UpdateProvisions). One trade at a time (Shared/Trade.h): while its
    // hands trade, OnPacketReceived keeps the trade status from the default handling.
    BotShared::Trade::Hands m_tradeHands;
    Life::GiveProvisionsState m_giveProvisions;
    Life::AskProvisionsState m_askProvisions;
};

#endif
