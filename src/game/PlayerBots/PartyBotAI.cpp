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

#include "PartyBotAI.h"
#include "Player.h"
#include "Corpse.h"
#include "CreatureAI.h"
#include "MotionMaster.h"
#include "ObjectMgr.h"
#include "PlayerBotMgr.h"
#include "Opcodes.h"
#include "World.h"
#include "WorldPacket.h"
#include "Spell.h"
#include "SpellAuras.h"
#include "Chat.h"
#include "Utilities/Random.h"
#include "RoleCapability.h"
#include "Commander/CommanderMgr.h"
#include "Commander/BotDirectives.h"
#include "Commander/Classes/ClassKit.h"
#include "Commander/Classes/RaceKit.h"
#include "Rotations/ClassRotation.h"
#include "Commander/VetoChecks.h"
#include "Commander/PartyCommander.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Coordination/Situation/GatherPlanner.h"
#include "Commander/Coordination/Situation/Summon.h"
#include "Commander/Behaviors/Combat/AreaDamageBehavior.h"
#include "Commander/Behaviors/Constraints.h"
#include "Commander/Behaviors/CurrentAction.h"
#include "Commander/Perception/Values/ValueModel.h"
#include "Commander/Perception/WorldModel.h"
#include "Commander/Perception/Values/RestValue.h"
#include "Commander/Perception/Profiles/SpellTags.h"
#include "Commander/Team.h"
#include "Companions/CompanionMgr.h"
#include "Companions/Affinity/TradeGate.h"
#include "Shared/Phrases.h"
#include "Shared/BotChat.h"
#include "Shared/Trade.h"
#include "Shared/GearUpgrade.h"
#include "Shared/GameNames.h"
#include "Shared/Portals.h"
#include "Life/Errands/ErrandCommon.h"
#include "GameObject.h"
#include "Log.h"
#include "SpellMgr.h"
#include "HealSpells.h"

#include <algorithm>
#include <random>

#define PB_MIN_FOLLOW_DIST 3.0f
#define PB_MAX_FOLLOW_DIST 6.0f
#define PB_MIN_FOLLOW_ANGLE 0.0f
#define PB_MAX_FOLLOW_ANGLE 6.0f

bool PartyBotAI::OnSessionLoaded(PlayerBotEntry* entry, WorldSession* sess)
{
    if (!m_race && !m_class)
    {
        sess->LoginPlayer(entry->playerGUID);
        return true;
    }

    return SpawnNewPlayer(sess, m_class, m_race, m_mapId, m_instanceId, m_x, m_y, m_z, m_o, sObjectAccessor.FindPlayer(m_cloneGuid));
}

void PartyBotAI::CloneFromPlayer(Player const* pPlayer)
{
    if (!pPlayer)
        return;

    if (pPlayer->GetLevel() != me->GetLevel())
    {
        me->GiveLevel(pPlayer->GetLevel());
        me->InitTalentForLevel();
        me->SetUInt32Value(PLAYER_XP, 0);
    }

    // Learn all of the target's spells.
    for (const auto& spell : pPlayer->GetSpellMap())
    {
        if (spell.second.disabled)
            continue;

        if (spell.second.state == PLAYERSPELL_REMOVED)
            continue;

        SpellEntry const* pSpellEntry = sSpellMgr.GetSpellEntry(spell.first);
        if (!pSpellEntry)
            continue;

        uint32 const firstRankId = sSpellMgr.GetFirstSpellInChain(spell.first);
        if (!me->HasSpell(spell.first))
            me->LearnSpell(spell.first, false, (firstRankId == spell.first && GetTalentSpellPos(firstRankId)));
    }

    // Unequip current gear
    for (int i = EQUIPMENT_SLOT_START; i < EQUIPMENT_SLOT_END; ++i)
        me->AutoUnequipItemFromSlot(i);

    // Copy gear from target.
    for (int i = EQUIPMENT_SLOT_START; i < EQUIPMENT_SLOT_END; ++i)
    {
        if (Item* pItem = pPlayer->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
        {
            me->SatisfyItemRequirements(pItem->GetProto());
            me->StoreNewItemInBestSlots(pItem->GetEntry(), 1, pItem->GetEnchantmentId(EnchantmentSlot(0)));
        }
    }
}

bool PartyBotAI::LootsForSelf() const
{
    // (a character of the leader's own account too: a real character, saved, account doc §3)
    return me && (sCompanionMgr.IsCompanion(me->GetGUIDLow()) || sCompanionMgr.IsAccountBot(me->GetGUIDLow()));
}

bool PartyBotAI::GetsFreeSupplies() const
{
    return !LootsForSelf();
}

bool PartyBotAI::GetsFreeAbilities() const
{
    return !LootsForSelf();
}

void PartyBotAI::StopErrands()
{
    Life::LootRun::Stop(me, m_lootRun);
    Life::GatherRun::Stop(me, m_gatherRun);
    Life::QuestObjectRun::Stop(me, m_questObjectRun);
    Life::QuestTurnIn::Stop(me, m_questTurnIn, m_rewardChooser);
    Life::QuestPickUp::Stop(me, m_questPickUp);
    Life::QuestItemUse::Stop(me, m_questItemUse);
    Life::NpcVisit::Stop(me, m_vendorVisit);
    Life::NpcVisit::Stop(me, m_trainerVisit);
    Life::NpcVisit::Stop(me, m_artisanVisit);
    Companions::ArtisanTrip::Stop(m_artisanTrip);
    Life::TownTrip::Stop(me, m_townTrip, m_tradeHands);
    Life::CraftRun::Stop(me, m_craftRun);
    Companions::ClassQuestFlow::Stop(m_classQuestFlow);
}

void PartyBotAI::RefreshLessonContext()
{
    m_lessonContextRole = m_role;
    m_lessonContextMs = std::max<uint32>(WorldTimer::getMSTime(), 1);
    m_lessonContext.tanks = m_role == ROLE_TANK;
    m_lessonContext.heals = m_role == ROLE_HEALER;
    m_lessonContext.usedChains.clear();
    auto use = [this](SpellEntry const* pSpell)
    {
        if (pSpell)
            m_lessonContext.usedChains.insert(sSpellMgr.GetFirstSpellInChain(pSpell->Id));
    };
    // What the rotation and the Commander cast (the class tables, the heal lists, the resurrection).
    for (SpellEntry const* pSpell : m_spells.raw.spells)
        use(pSpell);
    for (SpellEntry const* pSpell : m_spellListDirectHeal)
        use(pSpell);
    for (SpellEntry const* pSpell : m_spellListPeriodicHeal)
        use(pSpell);
    for (SpellEntry const* pSpell : m_spellListIndirectHeal)
        use(pSpell);
    use(m_resurrectionSpell);
}

std::vector<Life::WantedQuest> PartyBotAI::WantedPickUps(Player* pLeader, bool bonded) const
{
    std::vector<Life::WantedQuest> wanted;
    if (bonded)
    {
        for (uint32 questId : Companions::ClassQuestFlow::WantedPickUps(me, m_classQuestFlow))
        {
            Life::WantedQuest w;
            w.questId = questId;
            wanted.push_back(w);
        }
    }
    Life::QuestPickUp::AddLeaderQuests(me, pLeader, wanted);
    return wanted;
}

bool PartyBotAI::UpdateQuestTurnIn(Player* pLeader)
{
    bool const busy = Life::QuestTurnIn::Update(me, m_questTurnIn, pLeader, m_rewardChooser);
    for (Life::TurnedInQuest const& done : Life::QuestTurnIn::TakeTurnedIn(m_questTurnIn))
    {
        // One line per giver visit for the whole party (the once key), a new item worn its own.
        Commander::VoiceEvent e;
        e.speaker = me->GetObjectGuid();
        e.onceMs = 20 * IN_MILLISECONDS;
        std::string const item = done.itemId ? BotShared::GameNames::ItemName(done.itemId, pLeader) : std::string();
        if (done.wore && !item.empty())
        {
            e.key = "voice.quest.turnin.wore";
            e.tier = Commander::VoiceTier::Social;
            e.vars.emplace_back("item", item);
            e.once = "turnin.wore:" + std::to_string(done.giver.GetRawValue());
            me->HandleEmoteCommand(EMOTE_ONESHOT_CHEER);
        }
        else
        {
            e.key = "voice.quest.turnin";
            e.tier = Commander::VoiceTier::Flavor;
            e.once = "turnin:" + std::to_string(done.giver.GetRawValue());
        }
        sCommanderMgr.QueueVoice(me, std::move(e));
    }
    return busy;
}

bool PartyBotAI::UpdateProvisions(Player* pLeader)
{
    if (Life::GiveProvisions::Update(me, m_giveProvisions, pLeader, m_tradeHands, PersonalityTags()))
        return true;
    if (!LootsForSelf())
        return false;
    // (the other party bots here: the mages among them give, GiveProvisions)
    std::vector<Player*> mates;
    for (Player* pMember : Commander::Team::Members(me))
        if (pMember && pMember != me && pMember->IsBot() && pMember->IsInMap(me) &&
            dynamic_cast<PartyBotAI*>(pMember->AI()))
            mates.push_back(pMember);
    return Life::AskProvisions::Update(me, m_askProvisions, pLeader, mates, m_tradeHands, PersonalityTags());
}

void PartyBotAI::PauseProvisions()
{
    Life::GiveProvisions::Pause(me, m_giveProvisions, m_tradeHands);
    Life::AskProvisions::Pause(me, m_askProvisions, m_tradeHands);
}

bool PartyBotAI::UpdateWalkAway()
{
    Player* pWatcher = ObjectAccessor::FindPlayer(m_leaderGuid);
    if (Life::WalkAway::IsActive(m_walkAway))
        return pWatcher && Life::WalkAway::Update(me, m_walkAway, pWatcher);
    if (m_walkAwayTried || !pWatcher || !LootsForSelf() || sCompanionMgr.IsTemporary(me->GetGUIDLow()))
        return false;
    m_walkAwayTried = true;
    // (on its way to him, invisible: nobody sees it go)
    if (Life::Travel::IsActive(m_travel))
    {
        Life::Travel::Stop(me, m_travel);
        return false;
    }
    return Life::WalkAway::Begin(me, m_walkAway, pWatcher, PersonalityTags());
}

void PartyBotAI::Dismiss()
{
    // A companion is sent away like a player kicked from the party: it leaves on its own
    // (UpdateWalkAway); other party bots log out at once.
    if (LootsForSelf() && !sCompanionMgr.IsTemporary(me->GetGUIDLow()) && me->GetGroup())
        me->RemoveFromGroup();
    else
        botEntry->requestRemoval = true;
}

bool PartyBotAI::UpdateTravel(Player* pAnchor)
{
    uint32 const now = std::max<uint32>(WorldTimer::getMSTime(), 1);
    if (!Life::Travel::IsActive(m_travel))
    {
        if (!FarBehind(pAnchor))
        {
            m_farSinceMs = 0;
            return false;
        }
        if (!m_farSinceMs)
            m_farSinceMs = now;
        // A warlock will summon it once the team settles: it waits where it is, a while.
        if (Commander::GatherPlanner::SummonComing(pAnchor, me) &&
            WorldTimer::getMSTimeDiff(m_farSinceMs, now) < PB_SUMMON_WAIT_MS)
        {
            if (me->GetMotionMaster()->GetCurrentMovementGeneratorType() == FOLLOW_MOTION_TYPE)
            {
                me->GetMotionMaster()->Clear(false, true);
                me->GetMotionMaster()->MoveIdle();
            }
            return true;
        }
        Life::TravelPace pace;
        pace.timeRate = Companions::TraitEffects::TravelTimeRate(m_traitEffects);
        pace.skipsSailing = Companions::TraitEffects::SkipsSailing(m_traitEffects);
        Life::Travel::Begin(me, m_travel, pAnchor, PersonalityTags(), pace);
    }

    switch (Life::Travel::Update(me, m_travel, pAnchor, PersonalityTags()))
    {
        case Life::Travel::Step::OnRoad:
            return true;
        case Life::Travel::Step::Arrived:
            m_farSinceMs = 0;
            Commander::Arrival::Start(m_arrival, now);
            return true;
        default:
            m_farSinceMs = 0;
            return false;
    }
}

bool PartyBotAI::FarBehind(Player const* pAnchor) const
{
    if (!me->IsInMap(pAnchor))
        return true;
    return !me->IsWithinDistInMap(pAnchor, Commander::Summon::FAR_YD) && !Life::Travel::InSight(me, pAnchor);
}

std::vector<std::string> const& PartyBotAI::PersonalityTags() const
{
    static std::vector<std::string> const none;
    Companions::CompanionInfo const* info = me ? sCompanionMgr.Find(me->GetGUIDLow()) : nullptr;
    return info ? info->tags : none;
}

// Friend traits (Companions/Affinity/TraitEffects): the PlayerAI hooks.
void PartyBotAI::ChangeDamageTaken(Unit* /*attacker*/, uint32& damage, SpellEntry const* spell)
{
    Companions::TraitEffects::ChangeDamageTaken(me, m_traitEffects, damage, spell);
}
void PartyBotAI::ChangeHealingTaken(uint32& heal)
{
    Companions::TraitEffects::ChangeHealingTaken(me, m_traitEffects, heal);
}
void PartyBotAI::ChangeAuraDuration(SpellEntry const* spell, bool positive, int32& duration)
{
    Companions::TraitEffects::ChangeAuraDuration(m_traitEffects, spell, positive, duration);
}
void PartyBotAI::ChangeResurrection(uint32& health, uint32& mana)
{
    Companions::TraitEffects::ChangeResurrection(me, m_traitEffects, health, mana);
}
float PartyBotAI::RegenRate() const { return Companions::TraitEffects::RegenRate(m_traitEffects); }
float PartyBotAI::CastTimeRate(SpellEntry const* spell) const { return Companions::TraitEffects::CastTimeRate(m_traitEffects, spell); }
float PartyBotAI::PushbackRate() const { return Companions::TraitEffects::PushbackRate(m_traitEffects); }
float PartyBotAI::LockoutRate() const { return Companions::TraitEffects::LockoutRate(m_traitEffects); }
float PartyBotAI::FallDamageRate() const { return Companions::TraitEffects::FallDamageRate(m_traitEffects); }
float PartyBotAI::BreathRate() const { return Companions::TraitEffects::BreathRate(m_traitEffects); }
float PartyBotAI::SwimSpeedRate() const { return Companions::TraitEffects::SwimSpeedRate(m_traitEffects); }
float PartyBotAI::ReputationRate() const { return Companions::TraitEffects::ReputationRate(m_traitEffects); }
float PartyBotAI::RepairCostRate() const { return Companions::TraitEffects::RepairCostRate(m_traitEffects); }
float PartyBotAI::SellPriceRate(ItemPrototype const* proto) const { return Companions::TraitEffects::SellPriceRate(m_traitEffects, proto); }
float PartyBotAI::BuyPriceRate(ItemPrototype const* proto) const { return Companions::TraitEffects::BuyPriceRate(m_traitEffects, proto); }
float PartyBotAI::LootMoneyRate() const { return Companions::TraitEffects::LootMoneyRate(m_traitEffects); }
float PartyBotAI::DurabilityLossRate() const { return Companions::TraitEffects::DurabilityLossRate(m_traitEffects); }
float PartyBotAI::DetectedRangeRate() const { return Companions::TraitEffects::DetectedRangeRate(m_traitEffects); }
int32 PartyBotAI::StealthDetectBonus() const { return Companions::TraitEffects::StealthDetectBonus(m_traitEffects); }
float PartyBotAI::CraftExtraChance(SpellEntry const* spell) const { return Companions::TraitEffects::CraftExtraChance(m_traitEffects, spell); }
uint32 PartyBotAI::SavedReagent(SpellEntry const* spell) { return Companions::TraitEffects::SavedReagent(m_traitEffects, spell); }
float PartyBotAI::GatherExtraChance() const { return Companions::TraitEffects::GatherExtraChance(m_traitEffects); }

uint8 PartyBotAI::ChooseLootRoll(uint32 itemId) const
{
    if (!LootsForSelf())
        return ROLL_PASS;
    ItemPrototype const* pProto = sObjectMgr.GetItemPrototype(itemId);
    if (!pProto)
        return ROLL_PASS;
    // Need what it would wear; greed the rest that sells (as most players do).
    if (BotShared::GearUpgrade::IsUpgrade(me, pProto))
        return ROLL_NEED;
    return pProto->SellPrice ? ROLL_GREED : ROLL_PASS;
}

bool PartyBotAI::FillsUpAtOnce()
{
    Player* pLeader = GetPartyLeader();
    Player* pAnchor = pLeader ? FollowAnchor(pLeader) : nullptr;
    Commander::StaySpot stay;
    return pAnchor && FarBehind(pAnchor) && !IsArriving() && !sCommanderMgr.GetStay(this, stay);
}

Player* PartyBotAI::GetPartyLeader() const
{
    Group* pGroup = me->GetGroup();
    if (!pGroup)
        return nullptr;

    if (Player* originalLeader = ObjectAccessor::FindPlayerNotInWorld(m_leaderGuid))
    {
        // In case the original spawner is not in the same group as the bots anymore.
        if (pGroup != originalLeader->GetGroup())
            return nullptr;

        // In case the current leader is the bot itself.
        if (pGroup->GetLeaderGuid() == me->GetObjectGuid())
            return nullptr;

        return originalLeader;
    }
    return nullptr;
}

bool PartyBotAI::UsesSafeFollow() const
{
    // (on a transport the follow point is relative to the transport, FollowSpot does not handle
    // it: the original random offset there)
    return !me->GetTransport();
}

Commander::FollowSpot::Offset PartyBotAI::ChooseFollowOffset(Unit* pLeader, float heading)
{
    // The next check is one bot update away, and the follow generator runs ahead of the leader.
    float const lookaheadSec = sCommanderMgr.GetBotUpdateInterval() / 1000.0f + Commander::FollowSpot::FOLLOW_EXTRAPOLATION_SEC;
    return Commander::FollowSpot::Choose(me, pLeader, m_followOffset, lookaheadSec, heading);
}

void PartyBotAI::IssueFollow(Unit* pLeader, float heading, char const* why)
{
    uint32 const now = WorldTimer::getMSTime();
    FollowEvent& e = m_followEvents[m_followEventNext];
    m_followEventNext = (m_followEventNext + 1) % PB_FOLLOW_EVENTS;
    ++m_followIssues;
    e.ms = now;
    e.why = why;
    e.prevMotion = uint8(me->GetMotionMaster()->GetCurrentMovementGeneratorType());
    char const* owner = Directives().GetMovementOwner(now);
    e.owner = owner ? owner : "";

    m_followTargetGuid = pLeader->GetObjectGuid();
    // (on a transport: the original follow, the offset at the leader's facing)
    if (UsesSafeFollow())
        me->GetMotionMaster()->MoveFollow(pLeader, m_followOffset.dist, m_followOffset.angle,
                                          Commander::HumanFollow::StyleOf(me, heading));
    else
        me->GetMotionMaster()->MoveFollow(pLeader, m_followOffset.dist, m_followOffset.angle);
}

void PartyBotAI::StartFollowing(Unit* pLeader, char const* why)
{
    m_followChecked = false;
    float const heading = Commander::HumanFollow::Heading(me, pLeader);
    if (UsesSafeFollow())
    {
        // Fix: a random 3-6 yd at a random angle all around the leader (also in front of him)
        // and the same start, stop and turn for every bot: the bot's own place behind him.
        // Fix: that place was the same at every start (from the guid): a new one in its part of
        // the zone, spaced from the team (HumanFollow::Reroll).
        m_followOffset = Commander::HumanFollow::Reroll(me, pLeader, heading);
        m_followOffset = ChooseFollowOffset(pLeader, heading);
        m_followCheckX = pLeader->GetPositionX();
        m_followCheckY = pLeader->GetPositionY();
        m_followCheckO = heading;
        m_followChecked = true;
    }
    else
    {
        m_followOffset.dist = urand(PB_MIN_FOLLOW_DIST, PB_MAX_FOLLOW_DIST);
        m_followOffset.angle = frand(PB_MIN_FOLLOW_ANGLE, PB_MAX_FOLLOW_ANGLE);
    }
    IssueFollow(pLeader, heading, why);
}

void PartyBotAI::KeepFollowSpotSafe(Unit* pLeader)
{
    // Fix: following kept a random offset around the leader without looking at idle creatures;
    // the tank walked 4 yd to the leader's right front into the aggro range of the next cell's
    // prisoner (Stockade, stockade-20261101-174235). See Commander/Behaviors/Movement/FollowSpot.h.
    if (!UsesSafeFollow() || me->GetMotionMaster()->GetCurrentMovementGeneratorType() != FOLLOW_MOTION_TYPE)
    {
        m_followChecked = false;
        return;
    }

    float const heading = Commander::HumanFollow::Heading(me, pLeader);
    if (m_followChecked)
    {
        float const dx = pLeader->GetPositionX() - m_followCheckX;
        float const dy = pLeader->GetPositionY() - m_followCheckY;
        float const recheck = Commander::FollowSpot::FOLLOW_RECHECK_DIST;
        // (a leader standing still: a patrol may still come to the follow point)
        if (dx * dx + dy * dy < recheck * recheck &&
            Commander::FollowSpot::AngleGap(heading, m_followCheckO) < Commander::FollowSpot::FOLLOW_RECHECK_TURN &&
            !Commander::FollowSpot::NeedsRecheck(me, pLeader, m_followOffset, heading))
            return;
    }

    m_followCheckX = pLeader->GetPositionX();
    m_followCheckY = pLeader->GetPositionY();
    m_followCheckO = heading;
    m_followChecked = true;

    Commander::FollowSpot::Offset const o = ChooseFollowOffset(pLeader, heading);
    if (o.dist == m_followOffset.dist && o.angle == m_followOffset.angle)
        return;
    m_followOffset = o;
    IssueFollow(pLeader, heading, "spot");
}

void PartyBotAI::UpdateHumanFollow(Unit* pLeader, bool follows)
{
    // Drift while walking, look around / step / sit while the leader stands:
    // Commander/Behaviors/Movement/HumanFollow.h.
    follows = follows && UsesSafeFollow() &&
              me->GetMotionMaster()->GetCurrentMovementGeneratorType() == FOLLOW_MOTION_TYPE;
    Commander::FollowSpot::Offset next;
    if (!Commander::HumanFollow::Tick(me, pLeader, m_humanIdle, follows, m_followOffset, next))
        return;

    // The new place only where FollowSpot allows it (else its choice).
    float const heading = Commander::HumanFollow::Heading(me, pLeader);
    m_followOffset = next;
    m_followOffset = ChooseFollowOffset(pLeader, heading);
    m_followCheckX = pLeader->GetPositionX();
    m_followCheckY = pLeader->GetPositionY();
    m_followCheckO = heading;
    m_followChecked = true;
    IssueFollow(pLeader, heading, "human");
}

bool PartyBotAI::WaitForDeadLeader(Player* pLeader)
{
    if (!pLeader->IsDead())
        return false;
    // (MoveFollow is only the leader's follow, IssueFollow)
    if (me->GetMotionMaster()->GetCurrentMovementGeneratorType() == FOLLOW_MOTION_TYPE)
    {
        me->StopMoving();
        me->GetMotionMaster()->Clear(false, true);
        me->GetMotionMaster()->MoveIdle();
    }
    if (IsTrailFollowing())
        Commander::TrailFollow::Stop(this, m_trailFollow);
    m_followChecked = false;
    return true;
}

Player* PartyBotAI::GetFollowTarget(Player* pLeader)
{
    Player* pGuide = sCommanderMgr.GetGuide(this);
    if (!pGuide)
        return pLeader;
    return pGuide == me ? nullptr : pGuide;
}

Player* PartyBotAI::FollowAnchor(Player* pLeader)
{
    Player* pFollow = GetFollowTarget(pLeader);
    return pFollow ? pFollow : pLeader;
}

void PartyBotAI::StandAsGuide()
{
    // Only its own follow: a Commander walk or a `.partybot cometome` move is not stopped.
    if (me->GetMotionMaster()->GetCurrentMovementGeneratorType() == FOLLOW_MOTION_TYPE)
    {
        me->StopMoving();
        me->GetMotionMaster()->Clear(false, true);
        me->GetMotionMaster()->MoveIdle();
    }
    if (IsTrailFollowing())
        Commander::TrailFollow::Stop(this, m_trailFollow);
    m_followChecked = false;
    // (HumanFollow's sit while the leader stood: the guide gets up)
    m_humanIdle.sat = false;
}

bool PartyBotAI::UpdateSummon()
{
    ObjectGuid warlockGuid, targetGuid;
    Commander::SummonRole const role = sCommanderMgr.GetSummonRole(this, warlockGuid, targetGuid);
    // (a new ritual, maybe right after the last: the hands start over)
    if (role == Commander::SummonRole::None || m_summonHands.target != targetGuid)
        m_summonHands = SummonHands();
    if (role == Commander::SummonRole::None)
        return false;
    m_summonHands.target = targetGuid;
    auto say = [this, targetGuid](char const* key)
    {
        Player* pTarget = sObjectAccessor.FindPlayer(targetGuid);
        BotShared::Phrases::Vars vars;
        vars["leader"] = pTarget ? pTarget->GetName() : "";
        BotShared::BotChat::SayToParty(me, BotShared::Phrases::Say(BotShared::I18n::OfParty(me), key, PersonalityTags(), vars));
    };
    if (!me->IsAlive() || me->IsInCombat())
    {
        if (role == Commander::SummonRole::Caster)
        {
            sCommanderMgr.EndSummon(me, false);
            say("summon.broken");
        }
        return false;
    }

    uint32 const now = WorldTimer::getMSTime();
    if (role == Commander::SummonRole::Caster)
    {
        Player* pTarget = sObjectAccessor.FindPlayer(targetGuid);
        Spell const* pChannel = me->GetCurrentSpell(CURRENT_CHANNELED_SPELL);
        bool const channeling = pChannel && pChannel->m_spellInfo && pChannel->m_spellInfo->Id == Commander::Summon::RITUAL_OF_SUMMONING;
        if (!m_summonHands.cast)
        {
            SpellEntry const* pRitual = sSpellMgr.GetSpellEntry(Commander::Summon::RITUAL_OF_SUMMONING);
            if (!pTarget || !pRitual)
            {
                sCommanderMgr.EndSummon(me, false);
                return false;
            }
            Life::Errands::StopWalk(me);
            if (me->GetStandState() != UNIT_STAND_STATE_STAND)
                me->SetStandState(UNIT_STAND_STATE_STAND);
            // (the ritual summons the caster's selection)
            me->SetSelectionGuid(targetGuid);
            SpellCastResult const result = me->CastSpell(me, pRitual, false);
            if (result != SPELL_CAST_OK)
            {
                sLog.Out(LOG_BASIC, LOG_LVL_DETAIL, "[Summon] %s cannot start the ritual (result %u).", me->GetName(), uint32(result));
                sCommanderMgr.EndSummon(me, false);
                say("summon.broken");
                return false;
            }
            m_summonHands.cast = true;
            m_summonHands.castMs = now;
            return true;
        }
        if (me->IsNonMeleeSpellCasted(false) && !channeling)
            return true;    // (the ritual's cast time)
        if (channeling)
        {
            GameObject* pPortal = me->GetGameObject(Commander::Summon::RITUAL_OF_SUMMONING);
            if (pPortal && pPortal->GetGoState() != GO_STATE_ACTIVE &&
                WorldTimer::getMSTimeDiff(m_summonHands.castMs, now) > PB_SUMMON_HELPERS_MS)
            {
                me->InterruptSpell(CURRENT_CHANNELED_SPELL);
                sCommanderMgr.EndSummon(me, false);
                say("summon.nohelp");
                return false;
            }
            return true;
        }
        // The channel is over: the summon went out; he comes, or the confirmation runs out.
        if (!m_summonHands.sentMs)
        {
            m_summonHands.sentMs = now;
            sCommanderMgr.MarkSummonSent(me, targetGuid);
        }
        bool const forLeader = pTarget && pTarget == GetPartyLeader();
        // A teammate (a bot) accepts at once by itself: the warlock goes on with the next one.
        if (!forLeader)
        {
            sCommanderMgr.EndSummon(me, false);
            return false;
        }
        if (pTarget->IsInMap(me) && me->IsWithinDist(pTarget, PB_SUMMON_ARRIVED_YD))
        {
            sCommanderMgr.EndSummon(me, true);
            say("summon.arrived");
            return false;
        }
        if (WorldTimer::getMSTimeDiff(m_summonHands.sentMs, now) > PB_SUMMON_ACCEPT_MS)
            sCommanderMgr.EndSummon(me, false);
        return false;
    }

    // A helper: to the portal and click it, then keep still (its channel).
    Player* pWarlock = me->GetMap()->GetPlayer(warlockGuid);
    GameObject* pPortal = pWarlock ? pWarlock->GetGameObject(Commander::Summon::RITUAL_OF_SUMMONING) : nullptr;
    if (!pPortal || pPortal->GetGoState() == GO_STATE_ACTIVE)
        return false;
    if (pPortal->HasUniqueUser(me))
        return true;
    if (!me->IsWithinDistInMap(pPortal, INTERACTION_DISTANCE))
    {
        if (me->GetMotionMaster()->GetCurrentMovementGeneratorType() != POINT_MOTION_TYPE)
            me->GetMotionMaster()->MovePoint(0, pPortal->GetPositionX(), pPortal->GetPositionY(), pPortal->GetPositionZ(),
                                             MOVE_PATHFINDING | MOVE_RUN_MODE);
        return true;
    }
    if (me->IsNonMeleeSpellCasted(false))
        return true;
    Life::Errands::StopWalk(me);
    Life::Pastimes::WaitAround::End(me, m_waitAround);
    if (me->GetStandState() != UNIT_STAND_STATE_STAND)
        me->SetStandState(UNIT_STAND_STATE_STAND);
    pPortal->Use(me);
    return true;
}

bool PartyBotAI::FollowThroughPortal(Player* pLeader)
{
    GameObject* pPortal = BotShared::Portals::LeaderWentThrough(me, pLeader, PB_PORTAL_FOLLOW_YD);
    if (!pPortal)
        return false;
    if (me->IsMounted())
        me->RemoveSpellsCausingAura(SPELL_AURA_MOUNTED);
    if (!me->IsWithinDistInMap(pPortal, INTERACTION_DISTANCE))
    {
        if (me->GetMotionMaster()->GetCurrentMovementGeneratorType() != POINT_MOTION_TYPE)
            me->GetMotionMaster()->MovePoint(0, pPortal->GetPositionX(), pPortal->GetPositionY(), pPortal->GetPositionZ(),
                                             MOVE_PATHFINDING | MOVE_RUN_MODE);
        return true;
    }
    if (!me->IsStopped())
        me->StopMoving();
    me->GetMotionMaster()->Clear(false, true);
    me->GetMotionMaster()->MoveIdle();
    pPortal->Use(me);
    return true;
}

void PartyBotAI::HoldStay(Commander::StaySpot const& spot, bool free)
{
    // Its own follow stops, as the guide's (a Commander walk or a fight is not stopped).
    StandAsGuide();
    if (!free)
    {
        Life::Pastimes::WaitAround::End(me, m_waitAround);
        return;
    }

    // Back to the spot after a fight or a walk of the Commander.
    if (me->GetDistance2d(spot.x, spot.y, SizeFactor::None) > PB_STAY_BACK_YD)
    {
        Life::Pastimes::WaitAround::End(me, m_waitAround);
        if (me->GetMotionMaster()->GetCurrentMovementGeneratorType() != POINT_MOTION_TYPE)
            me->GetMotionMaster()->MovePoint(0, spot.x, spot.y, spot.z, MOVE_PATHFINDING | MOVE_RUN_MODE);
        return;
    }
    if (me->IsMoving())
        return;
    Life::Pastimes::WaitAround::Update(me, m_waitAround, WorldTimer::getMSTime());
}

void PartyBotAI::AddToPlayerGroup()
{
    Player* pPlayer = ObjectAccessor::FindPlayer(m_leaderGuid);
    if (!pPlayer)
        return;

    Group* group = pPlayer->GetGroup();
    if (!group)
    {
        group = new Group;
        // new group: if can't add then delete
        if (!group->Create(pPlayer->GetObjectGuid(), pPlayer->GetName()))
        {
            delete group;
            return;
        }
        sObjectMgr.AddGroup(group);
    }

    if (me->GetGroup() != group)
    {
        if (me->GetGroup())
            me->RemoveFromGroup();

        group->AddMember(me->GetObjectGuid(), me->GetName());
    }

    // Back with him into the group they kept (a companion, OnLeaving): if the lead passed to
    // this bot while he was away, it goes back to him (GetPartyLeader wants him leading).
    if (group->GetLeaderGuid() == me->GetObjectGuid())
        group->ChangeLeader(pPlayer->GetObjectGuid());
}

void PartyBotAI::HandBackLead()
{
    Group* group = me->GetGroup();
    if (!group || group->GetLeaderGuid() != me->GetObjectGuid())
        return;
    Player* pOwner = ObjectAccessor::FindPlayer(m_leaderGuid);
    if (!pOwner || pOwner->GetGroup() != group)
        return;
    group->ChangeLeader(pOwner->GetObjectGuid());
    BotShared::Phrases::Vars vars;
    vars["leader"] = pOwner->GetName();
    std::string const text = BotShared::Phrases::Say(BotShared::I18n::OfParty(me), "lead.back", PersonalityTags(), vars);
    if (!text.empty())
        BotShared::BotChat::SayToParty(me, text);
}

bool PartyBotAI::OnLeaving()
{
    if (!me || !LootsForSelf() || sCompanionMgr.IsTemporary(me->GetGUIDLow()))
        return false;
    // A trial partner leaving (the player logged out, sent him off, ...): the trial is over, he
    // leaves the group (recruiting doc §2.2).
    if (sCompanionMgr.IsTrial(me->GetGUIDLow()))
    {
        sCompanionMgr.OnTrialLeave(me->GetGUIDLow());
        return false;
    }
    // (his session gone: he logged out; FindPlayerNotInWorld also finds one still loading)
    Group const* group = me->GetGroup();
    bool const withLeader = group && group->IsMember(m_leaderGuid) &&
        !ObjectAccessor::FindPlayerNotInWorld(m_leaderGuid);
    Commander::StaySpot spot;
    bool const staying = m_restoreStay || sCommanderMgr.GetStay(this, spot);
    sCompanionMgr.OnLeave(me->GetGUIDLow(), withLeader, staying);
    return withLeader;
}

void PartyBotAI::OnPacketReceived(WorldPacket const* packet)
{
    //printf("Bot received %s\n", LookupOpcodeName(packet->GetOpcode()));
    switch (packet->GetOpcode())
    {
        case SMSG_LEARNED_SPELL:
        case SMSG_SUPERCEDED_SPELL:
        case SMSG_REMOVED_SPELL:
        {
            if (m_initialized)
                m_resetSpellData = true;
            return;
        }
        case SMSG_SUMMON_REQUEST:
        {
            // A warlock of the team summons it (the team gathers, Commander/Coordination/
            // Situation/GatherPlanner.h): accepted in UpdateAI, out of combat.
            m_summonOffered = true;
            return;
        }
        case SMSG_TRADE_STATUS:
        {
            if (!me)
                return;
            uint32 const status = packet->read<uint32>(0);
            // Its own trade (Shared/Trade: conjured supplies) is not the default accept-all.
            if (BotShared::Trade::OnStatus(me, m_tradeHands, status))
                return;
            // The player accepted his side: a companion takes only what its affinity allows
            // (Companions/Affinity/TradeGate); otherwise it cancels and says why.
            if (status == TRADE_STATUS_TRADE_ACCEPT)
            {
                Companions::TradeGate::Refusal const refusal = Companions::TradeGate::Check(me);
                if (refusal != Companions::TradeGate::Refusal::None)
                {
                    Player* trader = BotShared::Trade::OpenWith(me);
                    BotShared::Trade::CloseWindow(me);
                    BotShared::Phrases::Vars vars;
                    vars["leader"] = trader ? trader->GetName() : "";
                    std::string const text = BotShared::Phrases::Say(BotShared::I18n::OfParty(me), Companions::TradeGate::PhraseOf(refusal), PersonalityTags(), vars);
                    if (!text.empty())
                        BotShared::BotChat::SayToParty(me, text);
                    return;
                }
            }
            // Someone opens a trade: maybe the leader wants water (Life/Errands/GiveProvisions).
            if (status == TRADE_STATUS_BEGIN_TRADE && packet->size() >= sizeof(uint32) + sizeof(uint64))
                Life::GiveProvisions::OnTradeOpened(m_giveProvisions, ObjectGuid(packet->read<uint64>(sizeof(uint32))));
            break;
        }
        case SMSG_DUEL_REQUESTED:
        {
            // Party bots do not duel: decline (the request would hang until the challenger
            // walked away).
            auto data = std::make_unique<WorldPackets::Duel::DuelCancelled>();
            data->playerGuid = me->GetObjectGuid();
            me->GetSession()->QueuePacket(std::move(data));
            return;
        }
        case SMSG_PARTYKILLLOG:
        {
            // A companion keeps its round robin turn and loots it (Life/Errands/LootRun.h).
            if (!me || LootsForSelf())
                return;

            if (Group const* pGroup = me->GetGroup())
            {
                if (pGroup->GetLootMethod() == ROUND_ROBIN ||
                    pGroup->GetLootMethod() == GROUP_LOOT ||
                    pGroup->GetLootMethod() == NEED_BEFORE_GREED)
                {
                    ObjectGuid victimGuid = *(((uint64*)(*packet).contents()) + 1);
                    if (Creature* pCreature = me->GetMap()->GetCreature(victimGuid))
                    {
                        pCreature->m_Events.AddLambdaEventAtOffset([pCreature, guid = me->GetGUID()]()
                        {
                            if (pCreature->loot.roundRobinPlayer == guid)
                            {
                                // unassign loot from bot so real players can loot
                                pCreature->loot.roundRobinPlayer = 0;
                                pCreature->ForceValuesUpdateAtIndex(UNIT_DYNAMIC_FLAGS);
                            }
                        }, 1);
                    }
                }
            }
            
            return;
        }
        // (acknowledged below) A bot does not fall from the teleport point by itself.
        case SMSG_NEW_WORLD:
        case MSG_MOVE_TELEPORT_ACK:
            Commander::Landing::OnTeleport(m_landing);
            break;
    }

    CombatBotBaseAI::OnPacketReceived(packet);
}

void PartyBotAI::OnPlayerLogin()
{
    if (!m_initialized)
        me->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_SPAWNING);
    // Called over, it logs in at its place, which he may see: it must not pop up there (hidden
    // before the world shows it; it appears out of his sight and walks in, UpdateTravel).
    if (m_atSavedPosition && LootsForSelf())
        if (Player* pLeader = ObjectAccessor::FindPlayer(m_leaderGuid))
            if (Life::Travel::InSight(me, pLeader))
                Life::Travel::BeginHidden(me, m_travel);
}

void PartyBotAI::UpdateAI(uint32 const diff)
{
    // Every frame: the Commander's mid-cast checks, movement hands, stance swap (TeamBotAI).
    if (UpdateFrameHands(diff))
        return;

    m_updateTimer.Update(diff);
    if (m_updateTimer.Passed())
        m_updateTimer.Reset(sCommanderMgr.GetBotUpdateInterval());
    else
        return;

    Commander::BotTickCostScope commanderCost;

    if (!me->IsInWorld() || me->IsBeingTeleported())
        return;

    // A companion's friend traits: what they change on him, fights (also in combat).
    Companions::TraitEffects::Update(me, m_traitEffects);

    if (!m_initialized)
    {
        // Back with its leader before he is in the world: it stands where it logged out (still
        // spawning, nothing else runs) until he is, or leaves again (still his group's member).
        if (m_atSavedPosition && !ObjectAccessor::FindPlayer(m_leaderGuid))
        {
            uint32 const now = std::max<uint32>(WorldTimer::getMSTime(), 1);
            if (!m_waitLeaderSinceMs)
                m_waitLeaderSinceMs = now;
            else if (WorldTimer::getMSTimeDiff(m_waitLeaderSinceMs, now) >= PB_WAIT_LEADER_MS)
                botEntry->requestRemoval = true;
            return;
        }
        if (m_atSavedPosition)
            m_restoreStay = sCompanionMgr.TakeStay(me->GetGUIDLow());

        AddToPlayerGroup();

        if (m_race && m_class) // temporary character
        {
            if (m_level && m_level != me->GetLevel())
            {
                me->GiveLevel(m_level);
                me->InitTalentForLevel();
                me->SetUInt32Value(PLAYER_XP, 0);
            }

            if (!m_cloneGuid.IsEmpty())
            {
                CloneFromPlayer(sObjectAccessor.FindPlayer(m_cloneGuid));
                AutoAssignRole();
            }
            else
            {
                LearnPremadeSpecForClass();

                if (m_role == ROLE_INVALID)
                    AutoAssignRole();

                AutoEquipGear(sWorld.getConfig(CONFIG_UINT32_PARTY_BOT_AUTO_EQUIP));

                // fix client bug causing some item slots to not be visible
                if (Player* pLeader = GetPartyLeader())
                {
                    me->SetVisibility(VISIBILITY_OFF);
                    pLeader->UpdateVisibilityOf(pLeader, me);
                    me->SetVisibility(VISIBILITY_ON);
                }
            }
            me->UpdateSkillsToMaxSkillsForLevel();
        }
        else // loaded from db
        {
            if (m_role == ROLE_INVALID)
                AutoAssignRole();

            if (me->IsGameMaster())
                me->SetGameMaster(false);

            // (back with its leader: where it logged out)
            if (!m_atSavedPosition)
                me->TeleportTo(m_mapId, m_x, m_y, m_z, m_o);
        }

        ResetSpellData();
        PopulateSpellData();
        // The class is final now (a clone or a loaded character): give the bot its class kit.
        m_classKit = Commander::ClassKits::Create(*this);
        m_rotationCreated = false;
        Rotation();
        AddAllSpellReagents();
        me->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_SPAWNING);
        SummonPetIfNeeded();
        me->SetHealthPercent(100.0f);
        me->SetPowerPercent(me->GetPowerType(), 100.0f);

        uint32 newzone, newarea;
        me->GetZoneAndAreaId(newzone, newarea);
        me->UpdateZone(newzone, newarea);

        m_initialized = true;
        return;
    }

    if (m_resetSpellData)
    {
        ResetSpellData();
        PopulateSpellData();
        m_resetSpellData = false;
        m_lessonContextMs = 0;
    }
    if (m_lessonContextRole != m_role || !m_lessonContextMs ||
        WorldTimer::getMSTimeDiff(m_lessonContextMs, WorldTimer::getMSTime()) >= PB_LESSON_CONTEXT_MS)
        RefreshLessonContext();

    HandBackLead();
    Player* pLeader = GetPartyLeader();
    if (!pLeader)
    {
        // Sent away in front of him: it leaves as a person does, then logs out.
        if (UpdateWalkAway())
            return;
        botEntry->requestRemoval = true;
        return;
    }

    if (!pLeader->IsInWorld())
        return;

    // The leader's copy of this bot's quest; what happens to it is the class quest flow's.
    {
        Companions::MirrorSync::Event const mirrorEvent = Companions::MirrorSync::Update(me, pLeader, m_mirrorLink);
        if (mirrorEvent != Companions::MirrorSync::Event::None)
            Companions::ClassQuestFlow::OnMirrorEvent(me, pLeader, m_classQuestFlow, m_mirrorLink, mirrorEvent, PersonalityTags());
    }

    sCommanderMgr.OnBotUpdate(this);

    // It logged out with the leader while waiting: it waits where it is again (once its team
    // has a commander).
    if (m_restoreStay && sCommanderMgr.SetStay(me, true))
        m_restoreStay = false;

    // Told to wait here (Commander/Coordination/Situation/Stay.h): it follows nobody, is not
    // teleported to the leader and runs no errands away from its spot (HoldStay below).
    Commander::StaySpot staySpot;
    bool const staying = sCommanderMgr.GetStay(this, staySpot);
    if (!staying && m_waitAround.nextActMs)
        Life::Pastimes::WaitAround::End(me, m_waitAround);

    // He leaves by hearthstone or flight path: shall we wait? A waiting warlock: shall I summon
    // you back? (before the taxi wait below: the question comes as he takes off)
    // (his hearthstone, ours bound at his inn: home together, before anyone asks)
    Companions::HearthAlong::Update(me, pLeader, m_hearthAlong, staying, PersonalityTags());
    Companions::Departure::Update(me, pLeader, m_departure, staying, PersonalityTags());
    // A companion's hearthstone is bound where his is.
    if (LootsForSelf())
        Companions::HomeBind::Update(me, pLeader, m_homeBind, PersonalityTags());
    if (me->GetClass() == CLASS_WARLOCK)
        Companions::SummonOffer::Update(me, pLeader, m_summonOffer, staying, PersonalityTags());

    // (a bot waiting somewhere else does not care how the leader travels)
    if (pLeader->IsTaxiFlying() && !staying)
    {
        if (me->GetMotionMaster()->GetCurrentMovementGeneratorType())
        {
            me->GetMotionMaster()->Clear(false, true);
            me->GetMotionMaster()->MoveIdle();
        }
        return;
    }

    // Summoned by a warlock: it goes (the core drops an answer after the 2 minutes by itself).
    if (m_summonOffered && me->IsAlive() && !me->IsInCombat())
    {
        m_summonOffered = false;
        me->SummonIfPossible(true);
        return;
    }

    // Its part in a summoning ritual: before the body (a channel is a cast there).
    if (UpdateSummon())
        return;

    // Falling, controlled, dead, auto-repeating, casting (TeamBotAI).
    if (UpdateBody())
        return;

    // Hidden on its way in (Life/Errands/Travel): nothing else until it shows itself out of
    // sight (no errand by the leader while invisible next to him).
    if (Life::Travel::IsHidden(m_travel) && !me->IsInCombat())
    {
        UpdateTravel(FollowAnchor(pLeader));
        return;
    }

    bool const commanderOutOfCombat = !me->IsInCombat() && sCommanderMgr.WantsOutOfCombatControl(this);

    // Fix: resurrecting a dead member comes before eating/drinking: a resting bot stands up this
    // tick so UpdateOutOfCombatAI() can cast it. Targets with a pending request are ignored so
    // the bot does not stand up again and again.
    bool resurrectFirst = false;
    // The resurrection could be cast now if a body were in range and sight.
    bool const canResurrect = !me->IsInCombat() && m_resurrectionSpell && !me->IsMounted() &&
        me->IsSpellReady(m_resurrectionSpell) &&
        me->GetPower(Powers(m_resurrectionSpell->powerType)) >= Spell::CalculatePowerCost(m_resurrectionSpell, me);
    if (me->IsInCombat())
    {
        m_resurrectApproach = Commander::ResurrectApproachState();
        StopErrands();
        PauseProvisions();
        if (Life::CraftForPlayer::IsActive(m_craftForPlayer))
            Life::CraftForPlayer::Stop(me, m_craftForPlayer, Life::CraftForPlayer::Reason::Combat);
    }
    if (!me->IsInCombat())
    {
        if (m_resurrectionSpell && !me->IsMounted() && me->IsSpellReady(m_resurrectionSpell))
        {
            // An unreleased body, else the corpse of a member that released (a ghost).
            Player* pDead = SelectResurrectionTarget();
            Corpse* pReleased = pDead ? nullptr : SelectReleasedCorpse();
            ObjectGuid const deadGuid = pDead ? pDead->GetObjectGuid() :
                                        pReleased ? pReleased->GetOwnerGuid() : ObjectGuid();
            if (!deadGuid.IsEmpty())
            {
                if (me->GetPower(Powers(m_resurrectionSpell->powerType)) >= Spell::CalculatePowerCost(m_resurrectionSpell, me))
                {
                    // A resurrection that keeps failing (Rebirth without its reagent, a cast
                    // that never lands) must not keep the bot from drinking forever: after
                    // PB_RESURRECT_TRY_MS of trying without a success, let it rest for
                    // PB_RESURRECT_BACKOFF_MS, then try again.
                    // The window is per bot: keyed per corpse, two failing corpses took turns
                    // as the target and each switch restarted it, so the bot never drank.
                    // Switching corpses restarts it only when the last one was resurrected.
                    uint32 const now = WorldTimer::getMSTime();
                    if (m_resurrectTryGuid != deadGuid)
                    {
                        bool restart = m_resurrectTryGuid.IsEmpty();
                        if (!restart)
                        {
                            // (any map: a ghost waits at its graveyard)
                            Player* pLast = sObjectAccessor.FindPlayer(m_resurrectTryGuid);
                            restart = pLast && (pLast->IsAlive() || pLast->IsRessurectRequested());
                        }
                        if (restart)
                            m_resurrectTryMs = now;
                        m_resurrectTryGuid = deadGuid;
                    }
                    if (WorldTimer::getMSTimeDiff(m_resurrectTryMs, now) >= PB_RESURRECT_TRY_MS + PB_RESURRECT_BACKOFF_MS)
                        m_resurrectTryMs = now;
                    resurrectFirst = WorldTimer::getMSTimeDiff(m_resurrectTryMs, now) < PB_RESURRECT_TRY_MS;
                }
            }
            else
            {
                // No corpse left to resurrect (all alive or with a pending request): the next
                // death starts a fresh window. A corpse only out of reach or sight keeps the
                // window, or walking in and out of range would restart it every time.
                bool corpseLeft = false;
                for (GroupReference* itr = me->GetGroup()->GetFirstMember(); itr && !corpseLeft; itr = itr->next())
                {
                    Player* pMember = itr->getSource();
                    // (DEAD: released, its corpse may be here)
                    corpseLeft = pMember && pMember != me &&
                                 (pMember->GetDeathState() == CORPSE || pMember->GetDeathState() == DEAD) &&
                                 !pMember->IsRessurectRequested();
                }
                if (!corpseLeft)
                    m_resurrectTryGuid.Clear();
            }
        }

        // (eating and drinking: the Commander rest module, RestBehavior, through the hook below)

        // A dead member out of range or sight: walk to the body first (as a player does). One in
        // range and sight: no walk (UpdateOutOfCombatAI below casts it).
        if (resurrectFirst || !canResurrect || (commanderOutOfCombat && sCommanderMgr.IsRetreating(this)))
            Commander::ResurrectApproach::Stop(me, m_resurrectApproach);
        else if (Commander::ResurrectApproach::Update(this, m_resurrectApproach, m_resurrectionSpell))
            return;

        // A companion loots for itself after the fight (Life/Errands/LootRun.h). Not while the
        // Commander has an out-of-combat plan (a pull being set up, a retreat) or a resurrection
        // is about to be cast.
        // Then it skins and gathers (Life/Errands/GatherRun.h: what the leader can gather is his).
        // Then the quest objects its own quests need (Life/Errands/QuestObjectRun.h), then its
        // finished quests at a giver near the leader (Life/Errands/QuestTurnIn.h).
        // Last its business at a vendor / armorer and its class trainer near the leader
        // (Life/Errands/NpcVisit.h): sell, repair, buy supplies, learn. With nothing else to do,
        // it makes what it does not buy (Life/Errands/CraftRun.h: conjured food, poison vials).
        // Before all of them, conjured supplies for others or from a mage (UpdateProvisions):
        // the player asked for them, or a bot stands waiting by the mage.
        // (first a companion's affinity: levels gained with the player, money he gave for a trip)
        Companions::AffinitySources::Update(me, pLeader, m_affinitySources, Life::TownTrip::TakePaid(m_townTrip), PersonalityTags());
        // A trial partner (搭伙): the deal once, then his offer to stay when it is due.
        if (!resurrectFirst && !commanderOutOfCombat)
        {
            Companions::Trial::Update(me, pLeader, m_trial, PersonalityTags());
            // At affinity 100: once, may he join the player's account?
            // (kneeling while the player names him: he holds still)
            if (Companions::JoinOffer::Update(me, pLeader, m_joinOffer))
                return;
        }
        if (resurrectFirst || commanderOutOfCombat)
            PauseProvisions();
        else if (UpdateProvisions(pLeader))
            return;
        // What the player asked it to make with his materials (the talk window): it stands and
        // makes it before anything else of its own.
        if (!resurrectFirst && Life::CraftForPlayer::Update(me, m_craftForPlayer))
            return;
        if (resurrectFirst || commanderOutOfCombat || !LootsForSelf() || staying)
            StopErrands();
        else
        {
            // A trial partner (搭伙, recruiting doc §2.2) only plays along: loot, gather and the
            // quests; nothing a companion asks of the player (class quests, town trips and their
            // money, lessons, crafting).
            bool const bonded = sCompanionMgr.IsBonded(me->GetGUIDLow());
            // One of the player's own characters (account doc §3): its own trainer, vendor and
            // crafting errands, paid from its own purse; no class quest nor Artisan asks.
            bool const ownChar = !bonded && sCompanionMgr.IsAccountBot(me->GetGUIDLow());
            bool const errands = bonded || ownChar;
            m_townTrip.paysOwnWay = sCompanionMgr.PaysOwnWay(me->GetGUIDLow());
            if (bonded)
            {
                // First the class quest question of a visit to a capital (it asks while following,
                // and before the town trip's question: one open question per bot).
                Companions::ClassQuestFlow::Update(me, pLeader, m_classQuestFlow, m_mirrorLink, PersonalityTags());
                if (Companions::ClassQuestFlow::TakeSpellsChanged(m_classQuestFlow))
                    m_resetSpellData = true;
                // An Artisan trainer far away: asks once per capital visit, then leads the leader there.
                Companions::ArtisanTrip::Update(me, pLeader, m_artisanTrip, m_artisanTask, PersonalityTags());
            }
            // Out in the field: important lessons waiting (party chat, it keeps following).
            if (errands)
                Life::LessonReminder::Update(me, m_lessonReminder, pLeader, m_lessonContext, PersonalityTags());
            bool const busy = Life::LootRun::Update(me, m_lootRun, pLeader) ||
                Life::GatherRun::Update(me, m_gatherRun, pLeader) ||
                Life::QuestObjectRun::Update(me, m_questObjectRun, pLeader) ||
                Life::QuestItemUse::Update(me, m_questItemUse, pLeader) ||
                UpdateQuestTurnIn(pLeader) ||
                Life::QuestPickUp::Update(me, m_questPickUp, pLeader, WantedPickUps(pLeader, bonded)) ||
                (errands && (Life::NpcVisit::Update(me, m_vendorVisit, pLeader, m_vendorTask) ||
                    Life::NpcVisit::Update(me, m_trainerVisit, pLeader, m_trainerTask) ||
                    Life::NpcVisit::Update(me, m_artisanVisit, pLeader, m_artisanTask) ||
                    Life::TownTrip::Update(me, m_townTrip, pLeader, { &m_shoppingPlanner, &m_trainingPlanner, &m_professionPlanner }, PersonalityTags(),
                                           m_tradeHands) ||
                    Life::CraftRun::Update(me, m_craftRun, pLeader)));
            if (m_trainerTask.TakeLearned())
                m_resetSpellData = true;    // new ranks: the rotation picks them up
            if (busy)
                return;
        }

        // Back from a corpse run away from the leader: walk to him, no teleport. Not while a
        // resurrection here is about to be cast (the walk or the teleport would cancel it).
        // Not to a dead leader (WaitForDeadLeader).
        // (to the guide the party follows, Commander/Coordination/Situation/Guide.h; the guide
        // itself to the leader)
        // The leader went through a mage's portal: after him through it, before walking or
        // travelling to him (not a bot told to wait).
        if (!staying && !resurrectFirst && FollowThroughPortal(pLeader))
            return;

        // (a bot told to wait: neither, HoldStay)
        Player* pAnchor = FollowAnchor(pLeader);
        if (!staying && !resurrectFirst && !pAnchor->IsDead() && Commander::Arrival::Update(this, m_arrival, pAnchor))
            return;

        // Far behind: summoned, or it travels (UpdateTravel), every party bot; none is
        // teleported to the leader. Not to a dead leader: after a wipe the surviving bots were
        // teleported to the player's ghost at the graveyard, fell behind on its run back and
        // were teleported again every 100 yd. They wait where they are, by the corpse the ghost
        // runs to.
        if (!staying && !resurrectFirst && !pAnchor->IsDead() && UpdateTravel(pAnchor))
            return;
        if (Life::Travel::IsActive(m_travel) && (staying || resurrectFirst || pAnchor->IsDead()))
        {
            Life::Travel::Stop(me, m_travel);
            m_farSinceMs = 0;
        }
    }

    // Eating and drinking are the Commander's (RestBehavior): the hook runs out of combat while
    // something is missing, and a resting bot stands up when the Commander has something better
    // (the arbiter before any other action, the heal order step), here for a resurrection, or
    // once full (EndRestWhenFull).
    // The one it follows walked away from its rest (Commander/Behaviors/Movement/RestCatchUp.h):
    // it stands up and catches up before it sits again. Only the free rest: not with the
    // Commander's out-of-combat plan, at a stay spot or with a resurrection first.
    Commander::RestCatchUp::Update(this, m_restCatchUp,
        staying || resurrectFirst || commanderOutOfCombat ? nullptr : GetFollowTarget(pLeader));
    EndRestWhenFull();
    bool const needsRest = !me->IsInCombat() && NeedsRest();
    bool const commanderResting = !me->IsInCombat() && IsResting() && !resurrectFirst && !HoldsOffRest();
    // (also not a bot sitting idle while the leader stands, HumanFollow: it rises when it
    // follows again or the fight comes)
    // (and not one sitting at its stay spot, WaitAround)
    bool const idleSitting = (m_humanIdle.sat || (staying && Life::Pastimes::WaitAround::Seated(m_waitAround))) &&
        !me->IsInCombat() && !resurrectFirst;
    if (me->GetStandState() != UNIT_STAND_STATE_STAND && !commanderResting && !idleSitting)
        me->SetStandState(UNIT_STAND_STATE_STAND);

    // (not while retreating: a 10 s resurrection or a buff next to the enemies stops the run)
    if (!me->IsInCombat() && !me->IsMounted() && !commanderResting &&
        !(commanderOutOfCombat && sCommanderMgr.IsRetreating(this)))
    {
        UpdateOutOfCombatAI();

        if (m_isBuffing)
            return;

        if (me->IsNonMeleeSpellCasted())
            return;
    }

    // Original movement the Commander took over or checks, before the Commander hook: a tick in
    // which a module acts (a cast, the pull's hold) returns from the hook. Below it, the priest
    // kept following the player about 2.5 s into the fight, and out of combat under the pull's
    // hold the follow spot went unchecked; she stopped 17.8 yd from a wanderer's home and its
    // group of seven came (strat-20261107-164236).
    // The party follows a guide bot instead of the leader (Commander/Coordination/Situation/
    // Guide.h); null for the guide itself, which follows nobody.
    Player* const pFollow = GetFollowTarget(pLeader);
    {
        // (also while walking: the follow point moves with the leader)
        // The leader's way when the navmesh has none or only a long detour (a drop, a jump):
        // Commander/Behaviors/Movement/TrailFollow.h.
        if (staying)
            HoldStay(staySpot, !me->IsInCombat() && !me->GetVictim() && !commanderResting && !commanderOutOfCombat &&
                               !resurrectFirst && !IsMovementHeld() && !IsStanceControlled());
        else if (!pFollow)
            StandAsGuide();
        else
        {
            bool const follows = !me->GetVictim() && !IsMovementHeld() && !IsStanceControlled() &&
                !WaitForDeadLeader(pFollow) &&
                !Commander::TrailFollow::Consider(this, m_trailFollow, pFollow);
            // The guide changed (set, gone, back): the running follow is the old one's.
            if (follows && me->GetMotionMaster()->GetCurrentMovementGeneratorType() == FOLLOW_MOTION_TYPE &&
                m_followTargetGuid != pFollow->GetObjectGuid())
                StartFollowing(pFollow, "guide");
            else if (follows)
                KeepFollowSpotSafe(pFollow);
            UpdateHumanFollow(pFollow, follows);
        }

        // Fix: a healer that was following when the fight started kept following until it stood
        // still: the priest walked 15 yd behind the player into Magistrate Barthilas' Crowd
        // Pummel (strat-20261101-214026). Stop at once, also with a victim (a damage spell
        // between heals); the stance module picks the spot. A caster chase started before the
        // stance module took over (fight start): the same.
        StopTakenOverMovement();
    }

    // The Commander's tick: its behaviors pick the target, the place, the cast (TeamBotAI).
    if (RunCommanderHook(commanderOutOfCombat, needsRest, commanderResting))
        return;

    Unit* pVictim = me->GetVictim();

    if (!me->IsInCombat())
    {
        // Mount if leader is mounted and we don't have a target (not while waiting elsewhere).
        if (pLeader->IsMounted() && !me->GetVictim() && !staying)
        {
            if (!me->IsMounted())
            {
                // Leave shapeshift before mounting.
                if (me->IsInDisallowedMountForm() &&
                    me->GetDisplayId() != me->GetNativeDisplayId() &&
                    me->HasAuraType(SPELL_AURA_MOD_SHAPESHIFT))
                    me->RemoveSpellsCausingAura(SPELL_AURA_MOD_SHAPESHIFT);

                auto auraList = pLeader->GetAurasByType(SPELL_AURA_MOUNTED);
                if (!auraList.empty())
                {
                    bool oldStateCastTime = me->HasCheatOption(PLAYER_CHEAT_NO_CAST_TIME);
                    bool oldStatePower = me->HasCheatOption(PLAYER_CHEAT_NO_POWER);
                    me->SetCheatOption(PLAYER_CHEAT_NO_CAST_TIME, true);
                    me->SetCheatOption(PLAYER_CHEAT_NO_POWER, true);
                    me->CastSpell(me, (*auraList.begin())->GetId(), true);
                    me->SetCheatOption(PLAYER_CHEAT_NO_CAST_TIME, oldStateCastTime);
                    me->SetCheatOption(PLAYER_CHEAT_NO_POWER, oldStatePower);
                }
            }
        }
        else if (me->IsMounted())
            me->RemoveSpellsCausingAura(SPELL_AURA_MOUNTED);
    }

    // (follow spot check: before the Commander hook) The stance takeover may have started in
    // the hook's tick: stop its follow now, not one bot interval later.
    StopTakenOverMovement();

    if (!pVictim && !me->IsMoving() && !IsMovementHeld())
    {
        // Healers, ranged and melee damage dealers stand where the Commander stance modules
        // put them while the party fights, in combat themselves or not yet: following the leader walked the
        // priest into the next pack (Stockade, stockade-20261101-143825) and kept a healer
        // that had not healed yet with the player while the tank fought further away.
        if (pFollow && !staying && !IsStanceControlled() && !IsTrailFollowing() && !pFollow->IsDead() &&
            me->GetMotionMaster()->GetCurrentMovementGeneratorType() != FOLLOW_MOTION_TYPE)
            StartFollowing(pFollow, "restart");
    }

    // Swing at / chase the victim, face it, the rotation in combat (TeamBotAI).
    FightVictim(pVictim);
}

