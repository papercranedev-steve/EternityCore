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
#include "Commander/Behaviors/Combat/CastWatch.h"
#include "Commander/Behaviors/Combat/CastBreak.h"
#include "Commander/Behaviors/Movement/Backpedal.h"
#include "Commander/Behaviors/Movement/JumpCast.h"
#include "Commander/Behaviors/Movement/FollowSpot.h"
#include "Commander/Behaviors/Movement/HumanFollow.h"
#include "Commander/Behaviors/Movement/TrailFollow.h"
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
    // Seconds until CanTryToCastSpell would pass when only the spell's cooldown, the global
    // cooldown or a school lockout stand in the way: 0 = now, < 0 = something else does (no
    // mana, silenced, wrong form, out of range, a party check) or a permanent cooldown.
    float SpellReadyInSec(Unit const* pTarget, SpellEntry const* pSpellEntry) const;
    // The class kit's filler (Frostbolt, Fireball) is cast while its own slow / burn is still on
    // the target: the "already has the aura" rule left the mage idle until Frostbolt's 9 s slow
    // ran out (strat-20261102-205906, 13-19 s). A stacking aura too (Sunder Armor): each cast
    // adds a stack or refreshes it, and the cast is made for its threat (stockade-20261114-194753:
    // one Sunder per enemy, the tank had nothing but Heroic Strike left).
    bool IsSpammedOverOwnAura(SpellEntry const* pSpellEntry) const override;
    // PartyBotAI's checks on top of CombatBotBaseAI's (the Commander plan's constraints, no
    // AoE that hits an idle creature, no AoE that pulls aggro for non-tanks).
    bool PassesPartyCastChecks(Unit const* pTarget, SpellEntry const* pSpellEntry) const;
    // The server's cast checks the base check lacks or makes looser: combat range reach,
    // required weapon / shield, behind the target (PartyBotAI.cpp).
    bool PassesServerCastChecks(Unit const* pTarget, SpellEntry const* pSpellEntry) const;
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
    // The target-free part of CanCastWithStance: off cooldown and enough power now, or after
    // the stance swap (same rage rule). For the world model's ability facts (taunts).
    bool IsReadyWithStance(SpellEntry const* pSpellEntry) const;
    // Casts now, or starts the swap (SPELL_CAST_OK = swap started, the spell follows). A spell
    // that must be cast standing (cast time, a channel moving ends) stops a module's walk first
    // (StandStillFor).
    SpellCastResult DoCastWithStance(Unit* pTarget, SpellEntry const* pSpellEntry);
    // A ground area spell at the point x/y/z (BotAction::atPoint): the bot's own checks
    // (ready, power, form: CanTryToCastSpellAs on itself), the spell's range (talents
    // included) and line of sight to the point, the party's checks without a unit target
    // (opening hold, kept spells, Constraints::CastAtForbidden on the area).
    bool CanTryToCastSpellAt(float x, float y, float z, SpellEntry const* pSpellEntry) const;
    // StandStillFor, then CombatBotBaseAI::DoCastSpellAt.
    SpellCastResult DoCastAt(float x, float y, float z, SpellEntry const* pSpellEntry);
    // Ends a module's walk (its movement hold), then CombatBotBaseAI::DoCastFacing.
    SpellCastResult DoCastFacing(float orientation, SpellEntry const* pSpellEntry);
    // Execution: a point walk (a Commander MoveTo) does not pause for a cast the way chase and
    // follow do (TargetedMovementGenerator stops while IsNoMovementSpellCasted), so a channel
    // started on the way broke at once (Evocation, strat-20261108-141921). Stops it and ends
    // its movement hold when `pSpellEntry` must be cast standing (CurrentAction::NeedsStandingStill).
    void StandStillFor(SpellEntry const* pSpellEntry);
    // Breakable controls the plan keeps (TeamPlan::KeepsUnbroken) that this cast would hit:
    // the target, the area of an area spell, the enemy a chained hit jumps to, the enemies
    // next to the victim of a melee splash buff (Blade Flurry, Sweeping Strikes). Same rule
    // as the arbiter's (Constraints::CastForbidden, the neutral-creature rules too). Every
    // cast of the bot passes here (PassesPartyCastChecks): rotations too, while the party
    // fights (WorldModel::InFight), not only once the bot itself is in combat.
    bool WouldWakeKeptControl(Unit const* pTarget, SpellEntry const* pSpellEntry) const;
    // Every bot tick in combat: a melee splash buff on the bot while an enemy the plan keeps
    // controlled stands next to its victim is cancelled (Blade Flurry broke a Shackle,
    // strat-20261108-141921). The cast itself is refused by WouldWakeKeptControl.
    void CancelSplashNearKeptControl();
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
    Player* SelectResurrectionTarget() const;
    Player* SelectShieldTarget() const;
    bool ShouldAutoRevive() const;

    // Out of combat: the class kit's friendly dispels (ClassKit::Dispels) on the party, in
    // caster form only. In combat the Commander dispels (DispelBehavior).
    bool DispelOutOfCombat();
    // Both run the class rotation (Rotations/ClassRotation.h); out of combat after
    // resurrection and dispels, in combat followed by trinkets.
    void UpdateInCombatAI() final;
    void UpdateOutOfCombatAI() final;
    // The cast waiting for a stance swap (DoCastWithStance). True: it was cast or is still waiting.
    bool ContinueStanceSwap();

    // Commander (PlayerBots/Commander/). Directives are created on first use.
    Commander::BotDirectives& Directives();
    Commander::BotDirectives const& Directives() const;
    // What this bot's class can do (Commander/Classes/ClassKit.h). Created when the bot enters
    // the world (spells known); before that, on first use. Map thread of the bot.
    Commander::ClassKit const& Kit() const;
    // The class rotation (Rotations/ClassRotation.h). Created with the kit, made again when
    // the role changes.
    ClassRotation* Rotation();
    // Commander (PartyCommander::WakeBots): run the next bot update in the next frame instead
    // of after the rest of the bot interval. A longer wait set by a player command (pause,
    // attack stop: PlayerBotMgr.cpp) is kept.
    void WakeUp();
    bool IsMovementHeld() const;
    // Player commands (.partybot cometome, attackstart, attackstop, pull, pause, unpause in
    // PlayerBotMgr.cpp) call this: the player's order wins over a Commander movement hold left
    // from before. A Commander module may hold again on its next decision.
    void ClearMovementHold();
    // Walk backwards to `to` facing the enemies (Commander/Behaviors/Movement/Backpedal.h), order valid
    // for `durationMs`. False when it cannot walk there.
    bool OrderBackpedal(Commander::Position3 const& to, ObjectGuid face, uint32 durationMs);
    bool IsBackpedaling() const { return m_backpedal.active; }
    // Jump, turn in the air, cast the instant `pSpellEntry` facing `orientation`, land and run on
    // (Commander/Behaviors/Movement/JumpCast.h). False when it is not running a point walk.
    bool OrderJumpCast(SpellEntry const* pSpellEntry, float orientation);
    bool IsJumpCasting() const { return m_jumpCast.active; }
    // Walking the leader's recorded trail (Commander/Behaviors/Movement/TrailFollow.h).
    bool IsTrailFollowing() const { return m_trailFollow.active; }
    // The Commander keeps the Sap controller of a pull in stealth (CrowdControlBehavior,
    // Takeover::Stealth): the rogue rotation's stealth in / cancel stands down.
    bool IsStealthControlled() const;
    // Stunned, feared, polymorphed: break out with a trinket, Blink or a shapeshift (CombatBotBaseAI::
    // BreakCrowdControlEffects). Divine Shield / Ice Block only as the team rescue's duty
    // (CastRescueWhileControlled).
    void BreakControl();
    bool CastRescueWhileControlled();
    // While the party fights a Commander stance module places this healer, ranged (keeping a
    // caster distance), melee damage bot or tank (Takeover::Stance: HealerStanceBehavior,
    // RangedStanceBehavior, MeleeStanceBehavior, TankStanceBehavior), also before it is in
    // combat itself: UpdateAI does not follow the leader and stops a follow under way.
    bool IsStanceControlled() const;
    // IsStanceControlled and the bot is not a healer: its chase belongs to the stance module
    // (BeginChasing does not start one, UpdateAI drops one under way). Ranged: no 25 yd caster
    // chase; melee: the module chases at the angle it chose (OrderChase); tank: it holds its
    // place and chases only an enemy that does not come (OrderChase, angle 0).
    bool IsChaseControlled() const;
    // Commander melee stance (BotActionType::Chase): chase `pVictim` at `angle` (radians, from
    // its facing; MoveChase distance 1 as the original chase). False when it cannot (rooted,
    // dead victim). Restarted only when the victim or the angle changed.
    bool OrderChase(Unit* pVictim, float angle);
    // The angle of the chase this bot runs (OrderChase or the original one), or false when it
    // does not chase `pVictim`.
    bool GetChaseAngle(Unit const* pVictim, float& angle) const;
    // Mana per second the bots' drink (PB_SPELL_DRINK, no item) restores at the server's mana
    // rate; 0 without a mana pool. A fact for the Commander (PartyMemberFacts::drinkManaPerSec).
    float DrinkManaPerSec() const;
    // Health per second the bots' food (PB_SPELL_FOOD) restores at the server's health rate.
    float FoodHealthPerSec() const;
    bool IsDrinking() const;
    bool IsResting() const;     // the food or the drink is on
    // Health or mana (when it drinks) below Commander::RestValue::FULL_SHARE.
    bool NeedsRest() const;
    // Commander rest (BotActionType::Rest, Behaviors/Support/RestBehavior.h; the original eat and drink
    // is gone): eat when health is missing, drink when mana is, or keep sitting. False in
    // combat, while buffing, with a victim, or with nothing missing. More than 100 yd from the
    // leader: full at once (it is about to be teleported), false. The Commander decides when a
    // resting bot stands up (the arbiter before any other action, the heal order step).
    bool StartResting();
    // The drink off at full mana, the food off at full health unless a periodic damage aura
    // still ticks (poison, bleed: they do not stand it up, the food keeps up with them).
    // Called each tick before the stand-up check, so a full bot stands at once instead of
    // sitting out the aura (stockade-20261114-221943: full at 10 s, sat to 34 s).
    void EndRestWhenFull();
    // The Commander plan's constraints (TeamPlan::EngageBlock / ForbidsGapCloser, directive
    // types doc), asked by the rotation the same way the arbiter asks them for module proposals.
    // Engage: this bot must not chase / attack `pUnit` (a patrol's path unless it is the bot's
    // intercept, the player's "leave it alone"). Gap closer: `pSpell` charges and the pull's
    // tank must not charge now.
    bool IsEngageForbidden(Unit const* pUnit) const;
    bool IsGapCloserForbidden(SpellEntry const* pSpell) const;
    // Commander flee stop keeps this spell (TeamPlan::HoldsSpell): the rotation does not cast it.
    bool IsSpellHeld(SpellEntry const* pSpell) const;
    // The area phase reserves this bot (TeamPlan::ForbidsLongCast): no spell with a cast or
    // channel time but its area spells.
    bool IsLongCastHeld(SpellEntry const* pSpell) const;
    // Stops a follow, or a chase not ordered by the stance module (OrderChase), that a
    // Commander stance module took over (UpdateAI, before and after the Commander hook: the
    // takeover may start in the hook's tick).
    void StopTakenOverMovement();
    // Swinging at a victim in reach while standing still and not facing it: turn to it (UpdateAI).
    void FaceMeleeVictim(Unit* pVictim);
    // Follow the leader (UpdateAI): the bot's own offset and style (Commander/Behaviors/Movement/
    // HumanFollow.h), moved out of idle creatures' aggro range (FollowSpot.h); on a transport
    // the original random offset.
    void StartFollowing(Unit* pLeader);
    // While following: re-check the follow point after the leader moved or its heading turned enough.
    void KeepFollowSpotSafe(Unit* pLeader);
    bool UsesSafeFollow() const;
    Commander::FollowSpot::Offset ChooseFollowOffset(Unit* pLeader, float heading);
    void IssueFollow(Unit* pLeader, float heading);
    // Every update with a leader: HumanFollow::Tick (drift, idle look / step / sit).
    void UpdateHumanFollow(Unit* pLeader, bool follows);
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
    // In combat with the Commander heal module deciding heals (BotDirectives::IsTakenOver, Takeover::Healing).
    bool IsHealingControlled() const;
    // Casts the Commander's heal order of this tick (BotDirectives::GetHealOrder), once.
    bool CastHealOrder();
    // Commander::BotAction::prepSpell: puts the instant `pPrep` on itself right before the main
    // cast (true when null or already on). False = it could not, the main cast is not made.
    bool CastPrepSpell(SpellEntry const* pPrep);
    // Heal choice (CombatBotBaseAI::ChooseHeal) and shields use the Commander's measured
    // damage on the target (Value::IncomingOn); the base melee estimate while the target is
    // not in the snapshot yet.
    float GetIncomingDps(Unit const* pTarget) const override;
    // Hides CombatBotBaseAI::IsValidHostileTarget (all callers are in PartyBotAI.cpp): a party
    // member mind controlled by a creature is not attacked (Commander::VetoChecks); the bot the
    // Commander sent to wake the last controlled enemy may attack it (IsAssignedToBreakControl).
    bool IsValidHostileTarget(Unit const* pTarget) const;
    // In combat: the Commander plan sends this bot to wake `pTarget`, the first of the
    // controlled enemies that are all that is left (Commander/Coordination/Doctrine/ControlledEnd.h).
    bool IsAssignedToBreakControl(Unit const* pTarget) const;

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
    // Commander position module (Commander/Behaviors/Movement/PositioningBehavior.cpp): the area a melee
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
    // Follow offset given to MoveFollow, and the leader position / heading of its last safety check.
    Commander::FollowSpot::Offset m_followOffset;
    float m_followCheckX = 0.0f;
    float m_followCheckY = 0.0f;
    float m_followCheckO = 0.0f;
    bool m_followChecked = false;
    Commander::HumanFollow::IdleState m_humanIdle;
    // The last OrderChase (Commander melee stance): victim and angle. Cleared by an original
    // chase (BeginChasing, const).
    mutable ObjectGuid m_chaseVictim;
    float m_chaseAngle = 0.0f;
    // Commander mid-cast heal check (Commander/Behaviors/Combat/CastWatch.h), every frame in UpdateAI.
    Commander::CastWatchState m_castWatch;
    // Last area channel check (Commander/Behaviors/Combat/AreaDamageBehavior.h WatchChannel).
    uint32 m_areaChannelCheckMs = 0;
    // Commander stop of the bot's own cast (Commander/Behaviors/Combat/CastBreak.h), every frame in UpdateAI.
    Commander::CastBreakState m_castBreak;
    // Commander walk backwards (Commander/Behaviors/Movement/Backpedal.h), every frame in UpdateAI.
    Commander::BackpedalState m_backpedal;
    // Commander jump turn cast (Commander/Behaviors/Movement/JumpCast.h), every frame in UpdateAI.
    Commander::JumpCastState m_jumpCast;
    // Commander leader trail walk (Commander/Behaviors/Movement/TrailFollow.h), every frame in UpdateAI.
    Commander::TrailFollowState m_trailFollow;
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
