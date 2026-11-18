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
#include "Commander/Behaviors/Combat/AreaDamageBehavior.h"
#include "Commander/Behaviors/Constraints.h"
#include "Commander/Behaviors/CurrentAction.h"
#include "Commander/Perception/Values/ValueModel.h"
#include "Commander/Perception/WorldModel.h"
#include "Commander/Perception/Values/RestValue.h"
#include "Commander/Perception/Profiles/SpellTags.h"
#include "Companions/CompanionMgr.h"
#include "Shared/GearUpgrade.h"
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
    return me && sCompanionMgr.IsCompanion(me->GetGUIDLow());
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
    Life::QuestObjectRun::Stop(me, m_questObjectRun);
    Life::QuestTurnIn::Stop(me, m_questTurnIn, m_rewardChooser);
    Life::NpcVisit::Stop(me, m_vendorVisit);
    Life::NpcVisit::Stop(me, m_trainerVisit);
    Life::TownTrip::Stop(me, m_townTrip);
    Life::CraftRun::Stop(me, m_craftRun);
}

bool PartyBotAI::CatchesUpByTeleport(Player const* pAnchor) const
{
    if (LootsForSelf())
        return !me->IsInMap(pAnchor);
    return !me->IsWithinDistInMap(pAnchor, PB_TELEPORT_TO_LEADER_YD);
}

std::vector<std::string> const& PartyBotAI::PersonalityTags() const
{
    static std::vector<std::string> const none;
    Companions::CompanionInfo const* info = me ? sCompanionMgr.Find(me->GetGUIDLow()) : nullptr;
    return info ? info->tags : none;
}

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
    return pAnchor && CatchesUpByTeleport(pAnchor) && !IsArriving();
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

void PartyBotAI::IssueFollow(Unit* pLeader, float heading)
{
    m_followTargetGuid = pLeader->GetObjectGuid();
    // (on a transport: the original follow, the offset at the leader's facing)
    if (UsesSafeFollow())
        me->GetMotionMaster()->MoveFollow(pLeader, m_followOffset.dist, m_followOffset.angle,
                                          Commander::HumanFollow::StyleOf(me, heading));
    else
        me->GetMotionMaster()->MoveFollow(pLeader, m_followOffset.dist, m_followOffset.angle);
}

void PartyBotAI::StartFollowing(Unit* pLeader)
{
    m_followChecked = false;
    float const heading = Commander::HumanFollow::Heading(me, pLeader);
    if (UsesSafeFollow())
    {
        // Fix: a random 3-6 yd at a random angle all around the leader (also in front of him)
        // and the same start, stop and turn for every bot: the bot's own place behind him.
        m_followOffset = Commander::HumanFollow::Of(me).offset;
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
    IssueFollow(pLeader, heading);
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
    IssueFollow(pLeader, heading);
}

void PartyBotAI::UpdateHumanFollow(Unit* pLeader, bool follows)
{
    // Drift while walking, look around / step / sit while the leader stands:
    // Commander/Behaviors/Movement/HumanFollow.h.
    follows = follows && UsesSafeFollow() &&
              me->GetMotionMaster()->GetCurrentMovementGeneratorType() == FOLLOW_MOTION_TYPE;
    Commander::FollowSpot::Offset next;
    if (!Commander::HumanFollow::Tick(me, pLeader, m_humanIdle, follows, next))
        return;

    // The new place only where FollowSpot allows it (else its choice).
    float const heading = Commander::HumanFollow::Heading(me, pLeader);
    m_followOffset = next;
    m_followOffset = ChooseFollowOffset(pLeader, heading);
    m_followCheckX = pLeader->GetPositionX();
    m_followCheckY = pLeader->GetPositionY();
    m_followCheckO = heading;
    m_followChecked = true;
    IssueFollow(pLeader, heading);
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

    if (!m_initialized)
    {
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
    }

    Player* pLeader = GetPartyLeader();
    if (!pLeader)
    {
        botEntry->requestRemoval = true;
        return;
    }

    if (!pLeader->IsInWorld())
        return;

    // The leader's copy of this bot's quest (events: the class quest flow's, stage C4).
    Companions::MirrorSync::Update(me, pLeader, m_mirrorLink);

    sCommanderMgr.OnBotUpdate(this);

    // Dropping down on the leader's trail: a cast (buff, heal) would stop the fall in the air.
    if (Commander::TrailFollow::IsFalling(me, m_trailFollow) || Commander::Landing::IsFalling(me, m_landing))
        return;

    if (pLeader->IsTaxiFlying())
    {
        if (me->GetMotionMaster()->GetCurrentMovementGeneratorType())
        {
            me->GetMotionMaster()->Clear(false, true);
            me->GetMotionMaster()->MoveIdle();
        }
        return;
    }

    if (me->HasUnitState(UNIT_STATE_FEIGN_DEATH) && me->HasAuraType(SPELL_AURA_FEIGN_DEATH) &&
        ShouldEndFeignDeath())
        me->RemoveSpellsCausingAura(SPELL_AURA_FEIGN_DEATH);

    if (me->HasUnitState(UNIT_STATE_CAN_NOT_REACT_OR_LOST_CONTROL))
    {
        BreakControl();
        return;
    }

    // Dead: release and run the corpse like a player (Commander/Behaviors/Support/CorpseRun.h).
    if (me->IsDead())
    {
        Commander::CorpseRun::Update(this, m_corpseRun, m_arrival);
        return;
    }
    Commander::CorpseRun::OnAlive(me, m_corpseRun);

    // The pet (a triggered summon, no cast time): before the walk back, resting and mounting.
    // From the rotation's out-of-combat step only, a warlock back from a corpse run walked to
    // the leader, then rested, and the next pull came first: no Imp until the fight was over.
    if (!me->IsInCombat() && !me->IsMounted())
        SummonPetIfNeeded();

    // (before the auto-repeat step: it returns)
    UpdateWeaponStance();

    if (me->GetCurrentSpell(CURRENT_AUTOREPEAT_SPELL))
    {
        // Auto shot / wand would otherwise skip the Commander hook below (e.g. stop shooting near aggro).
        if (me->IsInCombat() && me->GetVictim() && sCommanderMgr.OnBotCombatUpdate(this))
            return;

        // Stop auto shot if no target.
        if (!me->GetVictim())
            me->InterruptSpell(CURRENT_AUTOREPEAT_SPELL, true);
        // Auto Shot survives a channel (SpellCaster::SetCurrentCastedSpell), so this branch also
        // runs while casting: the rotation's next shot broke the bot's own Mend Pet channel after
        // its mana was spent (strat-20261117-000150). Casting: only the Commander hook above may
        // end it (CastBreak weighs that).
        else if (me->IsNonMeleeSpellCasted(false, false, true))
            return;
        else if (ClassRotation* pRotation = Rotation())
            pRotation->UpdateWhileAutoRepeating();

        return;
    }

    // (Upstream "interrupt a pre-cast heal once its target is at full health", 2022, removed
    // 2026-11-16: it restarted a steady healer's heals on a full tank every bot tick,
    // strat-20261116-212207. A healer's heal that is no longer needed is CastWatch's.)
    if (me->IsNonMeleeSpellCasted(false, false, true))
        return;

    if (me->GetTargetGuid() == me->GetObjectGuid())
        me->ClearTarget();

    // Ammo given to the bot (trade, bag panel) goes into the ammo slot, so Shoot Bow / Gun /
    // Crossbow (ranged pull) and Auto Shot work. Cheap when nothing is missing.
    if (!me->IsInCombat())
        EquipAmmoFromBags();

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
        // Then the quest objects its own quests need (Life/Errands/QuestObjectRun.h), then its
        // finished quests at a giver near the leader (Life/Errands/QuestTurnIn.h).
        // Last its business at a vendor / armorer and its class trainer near the leader
        // (Life/Errands/NpcVisit.h): sell, repair, buy supplies, learn. With nothing else to do,
        // it makes what it does not buy (Life/Errands/CraftRun.h: conjured food, poison vials).
        if (resurrectFirst || commanderOutOfCombat || !LootsForSelf())
            StopErrands();
        else
        {
            bool const busy = Life::LootRun::Update(me, m_lootRun, pLeader) ||
                Life::QuestObjectRun::Update(me, m_questObjectRun, pLeader) ||
                Life::QuestTurnIn::Update(me, m_questTurnIn, pLeader, m_rewardChooser) ||
                Life::NpcVisit::Update(me, m_vendorVisit, pLeader, m_vendorTask) ||
                Life::NpcVisit::Update(me, m_trainerVisit, pLeader, m_trainerTask) ||
                Life::TownTrip::Update(me, m_townTrip, pLeader, { &m_shoppingPlanner, &m_trainingPlanner }, PersonalityTags()) ||
                Life::CraftRun::Update(me, m_craftRun, pLeader);
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
        Player* pAnchor = FollowAnchor(pLeader);
        if (!resurrectFirst && !pAnchor->IsDead() && Commander::Arrival::Update(this, m_arrival, pAnchor))
            return;

        // Teleport to leader if too far away. Fix: not to a dead leader: after a wipe the
        // surviving bots were teleported to the player's ghost at the graveyard, fell behind on
        // its run back and were teleported again every 100 yd. They wait where they are, by
        // the corpse the ghost runs to.
        // Companions only when on another map: on the same map they walk (a town trip, the
        // leader at his own trainer: CatchesUpByTeleport).
        if (!resurrectFirst && !pAnchor->IsDead() && CatchesUpByTeleport(pAnchor))
        {
            if (!me->IsStopped())
                me->StopMoving();
            me->GetMotionMaster()->Clear(false, true);
            me->GetMotionMaster()->MoveIdle();
            char name[128] = {};
            snprintf(name, sizeof(name), "%s", pAnchor->GetName());
            ChatHandler(me).HandleGonameCommand(name);
            return;
        }
    }

    // Eating and drinking are the Commander's (RestBehavior): the hook runs out of combat while
    // something is missing, and a resting bot stands up when the Commander has something better
    // (the arbiter before any other action, the heal order step), here for a resurrection, or
    // once full (EndRestWhenFull).
    EndRestWhenFull();
    bool const needsRest = !me->IsInCombat() && NeedsRest();
    bool const commanderResting = !me->IsInCombat() && IsResting() && !resurrectFirst;
    // (also not a bot sitting idle while the leader stands, HumanFollow: it rises when it
    // follows again or the fight comes)
    bool const idleSitting = m_humanIdle.sat && !me->IsInCombat() && !resurrectFirst;
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
        if (!pFollow)
            StandAsGuide();
        else
        {
            bool const follows = !me->GetVictim() && !IsMovementHeld() && !IsStanceControlled() &&
                !WaitForDeadLeader(pFollow) &&
                !Commander::TrailFollow::Consider(this, m_trailFollow, pFollow);
            // The guide changed (set, gone, back): the running follow is the old one's.
            if (follows && me->GetMotionMaster()->GetCurrentMovementGeneratorType() == FOLLOW_MOTION_TYPE &&
                m_followTargetGuid != pFollow->GetObjectGuid())
                StartFollowing(pFollow);
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

    // A victim that can no longer be hit (dead, under a control that breaks on damage, immune,
    // a charmed member) is dropped at once. Which enemy comes next is the Commander's
    // (TargetAssigner / TargetBehavior below).
    if (Unit* pOld = me->GetVictim())
        if (!IsValidHostileTarget(pOld))
            me->AttackStop();
    CancelSplashNearKeptControl();

    // Commander behaviors run before movement, so they can pick a target or a position
    // themselves. Since 2026-11-07 every target comes from there (target assignment doc): the
    // original target selection (leader's victim, own attackers, the first attacker of a
    // member, the tank's "defend party members") is deleted, not bypassed.
    if ((me->IsInCombat() || commanderOutOfCombat || needsRest || commanderResting) &&
        sCommanderMgr.OnBotCombatUpdate(this))
        return;

    // A healer not in combat during the party's fight: its heal step is here, after the hook
    // that set this tick's heal order (the out of combat AI above runs before it).
    if (!me->IsInCombat() && IsHealingControlled() && CastHealOrder())
        return;

    Unit* pVictim = me->GetVictim();

    if (!me->IsInCombat())
    {
        // Mount if leader is mounted and we don't have a target.
        if (pLeader->IsMounted() && !me->GetVictim())
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

    if (!me->IsMoving() && !IsMovementHeld())
    {
        if (!pVictim)
        {
            // Healers, ranged and melee damage dealers stand where the Commander stance modules
            // put them while the party fights, in combat themselves or not yet: following the leader walked the
            // priest into the next pack (Stockade, stockade-20261101-143825) and kept a healer
            // that had not healed yet with the player while the tank fought further away.
            if (pFollow && !IsStanceControlled() && !IsTrailFollowing() && !pFollow->IsDead() &&
                me->GetMotionMaster()->GetCurrentMovementGeneratorType() != FOLLOW_MOTION_TYPE)
                StartFollowing(pFollow);
        }
        else
        {
            if (!me->HasUnitState(UNIT_STATE_MELEE_ATTACKING) &&
               (GetRole() == ROLE_MELEE_DPS || m_role == ROLE_TANK) &&
                IsValidHostileTarget(pVictim) &&
                AttackStart(pVictim))
                return;

            switch (me->GetMotionMaster()->GetCurrentMovementGeneratorType())
            {
                case IDLE_MOTION_TYPE:
                case FOLLOW_MOTION_TYPE:
                    BeginChasing(pVictim);
                    break;
            }
        }
    }
    // Walking backwards (Commander Backpedal) faces the enemies coming after it: swing at the
    // victim without a chase (AttackStart would chase and end the walk). Without it the tank
    // walked back to its corner for 7 s after the shot without one swing, no rage and 0 threat
    // on five of the six enemies on it; a Counterspell and a few buffs turned them
    // (strat-20261108-200046).
    else if (pVictim && IsBackpedaling() && !me->HasUnitState(UNIT_STATE_MELEE_ATTACKING) &&
             (GetRole() == ROLE_MELEE_DPS || m_role == ROLE_TANK) && IsValidHostileTarget(pVictim))
        me->Attack(pVictim, true);

    KeepFacing(pVictim);

    if (me->IsInCombat())
        UpdateInCombatAI();
}

