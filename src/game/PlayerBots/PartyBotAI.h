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

#ifndef MANGOS_PARTYBOTAI_H
#define MANGOS_PARTYBOTAI_H

#include "CombatBotBaseAI.h"
#include "Group.h"
#include "ObjectAccessor.h"
#include "Commander/Behaviors/CastWatch.h"
#include "Commander/Behaviors/Backpedal.h"
#include "Commander/Behaviors/FollowSpot.h"
#include <memory>

namespace Commander { class BotDirectives; class ClassKit; }
class ClassRotation;

class PartyBotAI : public CombatBotBaseAI
{
public:

    PartyBotAI(Player* pLeader, Player* pClone, CombatBotRoles role, uint8 race, uint8 class_, uint8 level, uint32 mapId, uint32 instanceId, float x, float y, float z, float o)
        : CombatBotBaseAI(), m_race(race), m_class(class_), m_level(level), m_mapId(mapId), m_instanceId(instanceId), m_x(x), m_y(y), m_z(z), m_o(o)
    {
        m_role = role;
        m_roleLocked = role != ROLE_INVALID;
        m_leaderGuid = pLeader->GetObjectGuid();
        m_cloneGuid = pClone ? pClone->GetObjectGuid() : ObjectGuid();
        m_updateTimer.Reset(2000);
    }
    PartyBotAI(Player* pLeader, uint32 mapId, uint32 instanceId, float x, float y, float z, float o)
        : CombatBotBaseAI(), m_mapId(mapId), m_instanceId(instanceId), m_x(x), m_y(y), m_z(z), m_o(o)
    {
        m_role = ROLE_INVALID;
        m_leaderGuid = pLeader->GetObjectGuid();
        m_updateTimer.Reset(2000);
    }

    bool OnSessionLoaded(PlayerBotEntry* entry, WorldSession* sess) final;
    void OnPlayerLogin() final;
    void UpdateAI(uint32 const diff) final;
    void OnPacketReceived(WorldPacket const* packet) final;

    void CloneFromPlayer(Player const* pPlayer);
    void AddToPlayerGroup();

    // Best role from RoleCapability (class, talents, gear). The Commander "roles" assigner may
    // change it later by party composition while the role is not locked.
    void AutoAssignRole() override;
    // Sets the role; ROLE_INVALID = auto (AutoAssignRole, unlocked), else locked (chosen by the
    // user: the party assignment leaves it alone). Spell data is rebuilt on the next tick.
    void SetRole(CombatBotRoles role);
    // Role changed by the party assignment (stays unlocked). Spell data rebuilt on the next tick.
    void AssignRole(CombatBotRoles role);
    bool IsRoleLocked() const { return m_roleLocked; }

    bool CanTryToCastSpell(Unit const* pTarget, SpellEntry const* pSpellEntry) const final;
    // The class kit's filler (Frostbolt, Fireball) is cast while its own slow / burn is still on
    // the target: the "already has the aura" rule left the mage idle until Frostbolt's 9 s slow
    // ran out (strat-20261102-205906, 13-19 s).
    bool IsSpammedOverOwnAura(SpellEntry const* pSpellEntry) const override;
    // PartyBotAI's checks on top of CombatBotBaseAI's (no AoE next to crowd control marks, no
    // AoE that hits an idle creature, no AoE that pulls aggro for non-tanks).
    bool PassesPartyCastChecks(Unit const* pTarget, SpellEntry const* pSpellEntry) const;
    // An area spell hitting `pUnit` would start a new fight: a creature we may attack that is
    // not fighting (hostile or neutral), not a critter, not immune to area spells.
    bool WouldAreaSpellPullIdle(Unit const* pUnit) const;
    // Extra yards around an area spell's radius for WouldAreaSpellPullIdle.
    static constexpr float AOE_IDLE_MARGIN = 2.0f;

    // Warrior stance swaps (doc 2026-11-01-warrior-tank.md). A spell the current stance does not
    // allow (Thunder Clap or Mocking Blow in Defensive Stance, Taunt in Battle Stance) is cast
    // after one swap: the swap this tick, the spell as soon as the stance is up (UpdateAI checks
    // every frame, ContinueStanceSwap). The rotation swaps back to the home stance afterwards.
    // The stance spell that allows `pSpellEntry` when the current one does not; nullptr when no
    // swap is needed or none helps (not a warrior, stance unknown or on cooldown).
    SpellEntry const* StanceSwapFor(SpellEntry const* pSpellEntry) const;
    // CanTryToCastSpell now, or right after StanceSwapFor: with the rage Tactical Mastery keeps,
    // losing at most PB_STANCE_SWAP_MAX_RAGE_LOSS.
    bool CanCastWithStance(Unit const* pTarget, SpellEntry const* pSpellEntry) const;
    // Casts now, or starts the swap (SPELL_CAST_OK = swap started, the spell follows).
    SpellCastResult DoCastWithStance(Unit* pTarget, SpellEntry const* pSpellEntry);
    // Threat of `pSpellEntry` cast after StanceSwapFor relative to now (Defensive 1.3, others 0.8);
    // 1 without a swap.
    float StanceSwapThreatFactor(SpellEntry const* pSpellEntry) const;
    // Rage kept through a stance swap (Tactical Mastery, as in SpellAuras.cpp HandleAuraModShapeshift).
    uint32 RageKeptOnStanceSwap() const;
    static constexpr uint32 PB_STANCE_SWAP_MAX_RAGE_LOSS = 100;   // 10 rage
    static constexpr uint32 PB_STANCE_SWAP_TIMEOUT_MS = 1500;

    Player* GetPartyLeader() const;
    bool AttackStart(Unit* pVictim);
    // Mage, warlock, priest: AttackStart does not turn melee swings on.
    bool IsClothCaster() const;
    Unit* SelectAttackTarget(Player* pLeader) const;
    Unit* SelectPartyAttackTarget() const;
    Player* SelectResurrectionTarget() const;
    Player* SelectShieldTarget() const;
    Unit* GetMarkedTarget(RaidTargetIcon mark) const;
    bool CanUseCrowdControl(SpellEntry const* pSpellEntry, Unit* pTarget) const;
    bool DrinkAndEat();
    bool ShouldAutoRevive() const;
    bool IsValidDistancingTarget(Unit* pTarget, Unit* pEnemy);
    Unit* GetDistancingTarget(Unit* pEnemy);
    bool RunAwayFromTarget(Unit* pEnemy);
    bool CrowdControlMarkedTargets();
    bool EnterCombatDruidForm();
    bool ShouldEnterStealth() const;
    bool EnterStealthIfNeeded(SpellEntry const* pStealthSpell);

    bool CheckForDispelTargets();
    void UpdateInCombatAI() final;
    void UpdateOutOfCombatAI() final;
    void UpdateInCombatAI_Paladin() final;
    void UpdateOutOfCombatAI_Paladin() final;
    void UpdateInCombatAI_Shaman() final;
    void UpdateOutOfCombatAI_Shaman() final;
    void UpdateInCombatAI_Hunter() final;
    void UpdateOutOfCombatAI_Hunter() final;
    void UpdateInCombatAI_Mage() final;
    void UpdateOutOfCombatAI_Mage() final;
    void UpdateInCombatAI_Priest() final;
    void UpdateOutOfCombatAI_Priest() final;
    void UpdateInCombatAI_Warlock() final;
    void UpdateOutOfCombatAI_Warlock() final;
    void UpdateInCombatAI_Warrior() final;
    void UpdateOutOfCombatAI_Warrior() final;
    // The cast waiting for a stance swap (DoCastWithStance). True: it was cast or is still waiting.
    bool ContinueStanceSwap();
    void UpdateInCombatAI_Rogue() final;
    void UpdateOutOfCombatAI_Rogue() final;
    void UpdateInCombatAI_Druid() final;
    void UpdateOutOfCombatAI_Druid() final;

    // Commander (PlayerBots/Commander/). Directives are created on first use.
    Commander::BotDirectives& Directives();
    Commander::BotDirectives const& Directives() const;
    // What this bot's class can do (Commander/Classes/ClassKit.h). Created when the bot enters
    // the world (spells known); before that, on first use. Map thread of the bot.
    Commander::ClassKit const& Kit() const;
    // The class rotation moved out of this file (Rotations/ClassRotation.h); nullptr while the
    // class (or this role of it) still runs its UpdateInCombatAI_<Class> here. Created with the
    // kit, made again when the role changes.
    ClassRotation* Rotation();
    bool IsMovementHeld() const;
    // Player commands (.partybot cometome, attackstart, attackstop, pull, pause, unpause in
    // PlayerBotMgr.cpp) call this: the player's order wins over a Commander movement hold left
    // from before. A Commander module may hold again on its next decision.
    void ClearMovementHold();
    // Walk backwards to `to` facing the enemies (Commander/Behaviors/Backpedal.h), order valid
    // for `durationMs`. False when it cannot walk there.
    bool OrderBackpedal(Commander::Position3 const& to, ObjectGuid face, uint32 durationMs);
    bool IsBackpedaling() const { return m_backpedal.active; }
    bool IsTauntControlled() const;
    // A Commander module decides how this bot gets away from melee enemies (EscapeBehavior):
    // RunAwayFromTarget and the mage's Frost Nova / Blink block stand down.
    bool IsEscapeControlled() const;
    // In combat: `pUnit` stands in an area the Commander position module moves this bot out of
    // (Commander/Perception/HazardZones.h). False when the Commander or the module is off.
    bool IsInHazardZone(Unit const* pUnit) const;
    // In combat: the enemy `pUnit` stands in the path of a patrol the party moved off
    // (Commander PatrolMovePlan::avoid). Nobody chases it there. False when the Commander is off.
    bool IsInPatrolPath(Unit const* pUnit) const;
    // Healer in combat with the Commander "stance" module on: the module picks where it stands
    // (Commander/Behaviors/HealerStanceBehavior.h), so it does not follow the leader.
    bool HasCombatStance() const;
    // Follow the leader (UpdateAI): random offset as before; with the Commander on, moved out of
    // idle creatures' aggro range (Commander/Behaviors/FollowSpot.h).
    void StartFollowing(Unit* pLeader);
    // While following: re-check the follow point after the leader moved or turned enough.
    void KeepFollowSpotSafe(Unit* pLeader);
    bool UsesSafeFollow() const;
    Commander::FollowSpot::Offset ChooseFollowOffset(Unit* pLeader);
    // Hides CombatBotBaseAI::BeginChasing so every chase started from PartyBotAI honours the movement hold.
    void BeginChasing(Unit* pVictim) const;
    // Hide the CombatBotBaseAI versions so every dispel started from PartyBotAI skips
    // debuffs the Commander vetoed (Commander/VetoChecks.h).
    bool IsValidDispelTarget(Unit const* pTarget, SpellEntry const* pSpellEntry) const;
    Player* SelectDispelTarget(SpellEntry const* pSpellEntry) const;
    // Hides CombatBotBaseAI::FindAndHealInjuredAlly: skips the heal when the Commander vetoed it.
    // While the Commander heal module controls healing, casts its heal order instead (none: no heal).
    bool FindAndHealInjuredAlly(float selfHealPercent = 100.0f, float groupHealPercent = 100.0f);
    // Hides CombatBotBaseAI::FindAndPreHealTarget: off while the heal module controls healing.
    bool FindAndPreHealTarget();
    // In combat with the Commander heal module deciding heals (BotDirectives::IsHealingControlled).
    bool IsHealingControlled() const;
    // Casts the Commander's heal order of this tick (BotDirectives::GetHealOrder), once.
    bool CastHealOrder();
    // Heal choice (CombatBotBaseAI::ChooseHeal) and shields use the Commander's measured
    // damage on the target (Value::IncomingOn) when it is on; else the base melee estimate.
    float GetIncomingDps(Unit const* pTarget) const override;
    // The class rotations' reflex interrupts ask this first: true while the Commander interrupt
    // module controls this bot's interrupts (BotDirectives::IsInterruptControlled), or when
    // `pVictim` is immune to all of them.
    bool SkipsReflexInterrupt(Unit const* pVictim) const;
    // Hides CombatBotBaseAI::IsValidHostileTarget (all callers are in PartyBotAI.cpp): a party
    // member mind controlled by a creature is not attacked (Commander::VetoChecks).
    bool IsValidHostileTarget(Unit const* pTarget) const;

    std::vector<RaidTargetIcon> m_marksToCC;
    std::vector<RaidTargetIcon> m_marksToFocus;
    ShortTimeTracker m_updateTimer;
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
    bool m_resetSpellData = false;
    // The role was chosen by the user (.partybot setrole / load / add with a role, addbot with a
    // role); false = auto, the Commander party assignment may change it.
    bool m_roleLocked = false;
    // Resurrection before eating/drinking (UpdateAI): the last corpse tried, and since when the
    // bot has been trying without a success (per bot, whatever the corpse; empty guid = no window).
    // Longer than a 10 s Resurrection cast, so one slow cast that lands is not cut off.
    static constexpr uint32 PB_RESURRECT_TRY_MS = 15 * IN_MILLISECONDS;
    static constexpr uint32 PB_RESURRECT_BACKOFF_MS = 60 * IN_MILLISECONDS;
    ObjectGuid m_resurrectTryGuid;
    uint32 m_resurrectTryMs = 0;
    // Commander position module (Commander/Behaviors/PositioningBehavior.cpp): the area a melee
    // bot last stepped out of without its victim in reach. After that one step it fights inside
    // the area until it leaves it or the area is gone. Mutable: behaviors get a const bot.
    struct HazardGiveUp
    {
        ObjectGuid enemy;           // empty = none
        uint32 spellId = 0;
        uint32 movedMs = 0;         // when the step was proposed
        uint32 holdMs = 0;          // its movement hold
        bool left = false;          // seen outside the area after the step
        bool returned = false;      // seen inside again after the hold: no more out-of-reach steps
    };
    mutable HazardGiveUp m_hazardGiveUp;
    // Follow offset given to MoveFollow, and the leader position / facing of its last safety check.
    Commander::FollowSpot::Offset m_followOffset;
    float m_followCheckX = 0.0f;
    float m_followCheckY = 0.0f;
    float m_followCheckO = 0.0f;
    bool m_followChecked = false;
    // Commander mid-cast heal check (Commander/Behaviors/CastWatch.h), every frame in UpdateAI.
    Commander::CastWatchState m_castWatch;
    // Last area channel check (Commander/Behaviors/AreaDamageBehavior.h WatchChannel).
    uint32 m_areaChannelCheckMs = 0;
    // Commander walk backwards (Commander/Behaviors/Backpedal.h), every frame in UpdateAI.
    Commander::BackpedalState m_backpedal;
    // Casting or on the global cooldown in the last frame: the next bot update runs in the
    // frame this ends instead of up to a bot interval later (UpdateAI).
    bool m_wasBusy = false;
    // The spell DoCastWithStance swapped stance for (nullptr = none).
    struct StanceSwap
    {
        SpellEntry const* spell = nullptr;
        ObjectGuid target;          // empty = self
        uint32 startMs = 0;
    };
    StanceSwap m_stanceSwap;
    // shared_ptr (not unique_ptr) so the type can stay incomplete in this header.
    std::shared_ptr<Commander::BotDirectives> m_directives;
    // Class kit (Commander/Classes/ClassKit.h); mutable for the on-first-use fallback in Kit().
    mutable std::shared_ptr<Commander::ClassKit const> m_classKit;
    // Class rotation (Rotations/ClassRotation.h); shared_ptr for the incomplete type.
    std::shared_ptr<ClassRotation> m_rotation;
    bool m_rotationCreated = false;
    CombatBotRoles m_rotationRole = ROLE_INVALID;   // role m_rotation was made for
};

#endif
