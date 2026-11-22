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
 * A bot the Commander (PlayerBots/Commander/) commands: one member of a team of any size, from a
 * bot alone in the open world (a team of one) to a raid (.kiro/steering/ai-architecture.md).
 * The body and its hands: the Commander's entry points (directives, orders, takeovers), cast
 * checks and stance swaps, resting, resurrection, the class kit and rotation, the role.
 * What the bot does besides fighting (follow a player's party, errands, a population bot's
 * life) is the subclass's: PartyBotAI (a member of a player's party), later the field bots.
 * Map thread of the bot.
 */

#ifndef MANGOS_TEAMBOTAI_H
#define MANGOS_TEAMBOTAI_H

#include "CombatBotBaseAI.h"
#include "Group.h"
#include "ObjectAccessor.h"
#include "Commander/Behaviors/Combat/CastWatch.h"
#include "Commander/Behaviors/Combat/CastBreak.h"
#include "Commander/Behaviors/Movement/Backpedal.h"
#include "Commander/Behaviors/Movement/JumpCast.h"
#include "Commander/Behaviors/Movement/TrailFollow.h"
#include "Commander/Behaviors/Movement/Arrival.h"
#include "Commander/Behaviors/Movement/Landing.h"
#include "Commander/Behaviors/Support/CorpseRun.h"
#include "Commander/Behaviors/Support/ResurrectApproach.h"
#include "Commander/Behaviors/Movement/RestCatchUp.h"
#include "Shared/FoodAndDrink.h"
#include <memory>

namespace Commander { class BotDirectives; class ClassKit; }
class Corpse;
class ClassRotation;

class TeamBotAI : public CombatBotBaseAI
{
public:
    TeamBotAI() : CombatBotBaseAI() { m_updateTimer.Reset(2000); }

    // The team's leader the bot plays with (the player of a party), null without one.
    virtual Player* GetPartyLeader() const = 0;
    // Out of combat, about to be put next to its team at once (a party bot far behind the
    // leader is teleported there): StartResting fills it up at once instead of sitting down.
    virtual bool FillsUpAtOnce() { return false; }
    // A team of bots looks for its own fights (Commander HuntAssigner) unless a member does
    // not: a road traveller only fights what attacks it (ambient flow doc §3.2).
    virtual bool SeeksFights() const { return true; }

    // Every frame, before the bot update's throttle (the subclass's UpdateAI): the Commander's
    // mid-cast checks (CastWatch, area channel, CastBreak), the movement hands (Backpedal,
    // JumpCast, TrailFollow, Landing), the stance swap's spell, and the wake-up when a cast or
    // the global cooldown ends. True: the stance swap cast this frame, nothing else this frame.
    bool UpdateFrameHands(uint32 diff);

    // The commanded bot update in three steps; the subclass's UpdateAI puts its own life
    // (following, errands, a population bot's routine) between them:
    //  1. UpdateBody: falling after a teleport or the leader's trail, feign death, controls,
    //     death (the corpse run), the pet, the weapons in hand, an auto-repeat shot, a cast in
    //     progress. True: nothing else this tick.
    //  2. RunCommanderHook: drops a victim that can no longer be hit, then the Commander's
    //     behaviors (in combat, while it controls the out-of-combat time, while resting or
    //     needing rest) and the heal order of a healer not in combat. True: acted.
    //  3. FightVictim: swing at / chase the victim (standing, not held), face it, the class
    //     rotation in combat.
    bool UpdateBody();
    bool RunCommanderHook(bool outOfCombatControl, bool needsRest, bool resting);
    void FightVictim(Unit* pVictim);
    // `pVictim` is the creature its team of bots hunts (TeamPlan::hunt) and not fighting yet:
    // the rotation opens on it out of combat.
    bool IsOpeningOn(Unit const* pVictim) const;

    // What this bot does for its team (Commander PartyMemberFacts::functions, TeamFunction bits
    // in Commander/CommanderTypes.h): its role's own plus what nobody else in the team does
    // (a lone healer deals damage too; doc .kiro/docs/2026-11-19-any-size-team.md). Without a
    // Commander or before its first refresh: the role's own.
    uint8 TeamFunctions() const;
    // Heals the team: the class rotation's heal step (the heal module's orders in a fight).
    bool HealsForTeam() const;
    // Deals damage for the team: the class rotation's damage step. A bot that also heals takes
    // its heal step first (the heal module orders a heal when one is needed) and its damage
    // step only when it cast nothing there.
    bool DealsDamageForTeam() const;
    // A healer's damage this tick (Commander HealerDamageTier, healer damage tiers doc 2026-11-21):
    // the plan's tier, FREE when its mana condition fails (SPARE below TIER2_MANA_PCT, FULL
    // below HealerManaReserve unless the plan waives the reserve). Not in the plan (another role,
    // the Commander or the module off): FULL when it deals damage for the team, else NONE.
    uint8 HealerDamageNow() const;
    // RESERVE_HEALS x the mana of its biggest single target direct heal.
    float HealerManaReserve() const;
    // A healer the plan sends into melee (Commander HealerDamageDuty::melee: a paladin or shaman
    // in the open world): the melee stance module places it, it chases and swings like a melee
    // damage dealer, and stops swinging near aggro (FightVictim).
    bool IsMeleeHealer() const;

    // Best role from RoleCapability (class, talents, gear). The Commander "roles" assigner may
    // change it later by party composition while the role is not locked.
    void AutoAssignRole() override;
    // Sets the role; ROLE_INVALID = auto (AutoAssignRole, unlocked), else locked (chosen by the
    // user: the party assignment leaves it alone). Spell data is rebuilt on the next tick.
    void SetRole(CombatBotRoles role);
    // Role changed by the party assignment (stays unlocked). Spell data rebuilt on the next tick.
    void AssignRole(CombatBotRoles role);
    bool IsRoleLocked() const { return m_roleLocked; }
    // `player` can resurrect others out of combat: a bot by its resurrection spell, a player
    // by class and level (priests from 10, paladins and shamans from 12).
    static bool CanResurrectOthers(Player const* player);
    // Contingencies (Commander CrisisAssigner, user 2026-11-15): a role for the rest of this
    // fight (a stand-in tank, a spare healer), also over a locked role; the role before is kept
    // and given back by RestoreRole (the lock is never touched). Spell data rebuilt next tick.
    void SetEmergencyRole(CombatBotRoles role);
    void RestoreRole();
    bool HasEmergencyRole() const { return m_roleBeforeEmergency != ROLE_INVALID; }

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
    // TeamBotAI's checks on top of CombatBotBaseAI's (the Commander plan's constraints, no
    // AoE that hits an idle creature, no AoE that pulls aggro for non-tanks).
    bool PassesPartyCastChecks(Unit const* pTarget, SpellEntry const* pSpellEntry) const;
    // The server's cast checks the base check lacks or makes looser: combat range reach,
    // required weapon / shield, behind the target (TeamBotAI.cpp).
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

    bool AttackStart(Unit* pVictim);
    // Shoot (5019) is learned and an unbroken wand is equipped (the server's own item rule,
    // Player::HasItemFitToSpellReqirements). Shoot has no global cooldown (spell_template:
    // startRecoveryTime 0) and any other cast interrupts it.
    bool CanShootWand() const;
    // Mage, warlock, priest: AttackStart does not turn melee swings on.
    bool IsClothCaster() const;
    // A member whose body lies here unreleased (CORPSE) in range and sight of the resurrection.
    Player* SelectResurrectionTarget() const;
    // The corpse on this map of a member that released its spirit (a ghost, maybe elsewhere),
    // in range and sight: resurrections are cast at the corpse, as a client does.
    Corpse* SelectReleasedCorpse() const;
    // The resurrection spell (or `pSpell`: Rebirth in combat) at a released corpse
    // (SpellCastTargets corpse target). True: cast.
    bool CastResurrectionAtCorpse(Corpse* pCorpse, SpellEntry const* pSpell = nullptr);
    // Commander::BotActionType::Resurrect: `pSpell` on the dead member `deadGuid`, its body here
    // (not released) or its corpse on this map (a ghost anywhere); in range and sight, not
    // asked already. True: cast.
    bool CastResurrection(SpellEntry const* pSpell, ObjectGuid deadGuid);
    Player* SelectShieldTarget() const;
    // Back alive away from the leader, walking to him (Commander/Behaviors/Movement/Arrival.h).
    bool IsArriving() const { return m_arrival.active; }

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
    // Feign Death (hunter): up again once it did its job, at least FEIGN_DEATH_MIN_MS after the
    // cast and nothing still hitting the bot; it stays down while the wipe plan hides it.
    static constexpr uint32 FEIGN_DEATH_MIN_MS = 1000;
    bool ShouldEndFeignDeath() const;
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
    // What the bot eats and drinks: bots with free supplies the aura spells PB_SPELL_FOOD /
    // PB_SPELL_DRINK (no item), companions the best food / drink in their bags
    // (Shared/FoodAndDrink; out of it they do not rest for it, they bought it in town:
    // Life/Errands/MerchantTasks.h).
    // Mana per second its drink restores at the server's mana rate (the one on now, else the
    // one it would start); 0 without a mana pool or without drink. A fact for the Commander
    // (PartyMemberFacts::drinkManaPerSec).
    float DrinkManaPerSec() const;
    // Health per second its food restores at the server's health rate; 0 without food.
    float FoodHealthPerSec() const;
    bool IsDrinking() const;
    bool IsEating() const;
    bool IsResting() const;     // the food or the drink is on
    // Seconds until the food and drink on now have filled it up (the longer of the two, their
    // own rates without regeneration); 0 when not resting.
    float RestSecondsLeft() const;
    // Catching up with a leader who walked away (RestCatchUp): no resting until then.
    bool HoldsOffRest() const { return m_restCatchUp.active; }
    // Health (when it has food) or mana (when it drinks) below Commander::RestValue::FULL_SHARE.
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
    // The eating / drinking aura on the bot now, or null.
    SpellEntry const* ActiveRest(BotShared::FoodAndDrink::Kind kind) const;
    // What it would eat / drink next: the free aura spell (`item` null), a bag item, or nothing.
    SpellEntry const* NextRest(BotShared::FoodAndDrink::Kind kind, Item** item = nullptr) const;
    float RestRatePerSec(BotShared::FoodAndDrink::Kind kind) const;
    // Starts eating / drinking NextRest. True: started.
    bool BeginRest(BotShared::FoodAndDrink::Kind kind);
    // The food or drink spell used but not cast yet: an instant with a global cooldown is cast
    // at the next spell update, its aura comes only then.
    bool IsRestPending() const;
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
    // A cast time damage spell at an enemy the others kill before it lands, or periodic damage on
    // one that dies before it pays (Constraints::WastedCast).
    bool IsCastWasted(Unit const* pTarget, SpellEntry const* pSpell) const;
    // A healer's damage spell at an enemy that would pull it off its holder
    // (Commander::ThreatBehavior::DamageNearAggro, the damage dealers' stop rule). Healers only:
    // the damage dealers stop by the threat module's HoldFire, which would skip a healer's heal
    // step as well (healer damage tiers doc, 2026-11-21).
    bool IsDamagePullingAggro(Unit const* pTarget, SpellEntry const* pSpell) const;
    // The same rule for any hit of a healer on `pTarget` (its melee swings, FightVictim).
    bool IsSwingPullingAggro(Unit const* pTarget) const;
    // Stops a follow, or a chase not ordered by the stance module (OrderChase), that a
    // Commander stance module took over (UpdateAI, before and after the Commander hook: the
    // takeover may start in the hook's tick).
    void StopTakenOverMovement();
    // Standing still (UpdateAI): turn to the unit of a cast in progress or an auto-repeat shot
    // whose spell checks facing, else to a melee victim in reach outside the swing arc.
    void KeepFacing(Unit* pVictim);
    // Every tick (UpdateAI): the weapons in hand and the melee attack as a player's client sets
    // them: while Auto Shot or Shoot repeats the ranged weapon, and no melee attack out of reach;
    // the melee weapons after a melee attack starts or the fight ends.
    void UpdateWeaponStance();
    // Hides CombatBotBaseAI::BeginChasing so every chase started from TeamBotAI honours the movement hold.
    void BeginChasing(Unit* pVictim) const;
    // Hide the CombatBotBaseAI versions so every dispel started from TeamBotAI skips
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
    // Hides CombatBotBaseAI::IsValidHostileTarget (all callers are in TeamBotAI.cpp and PartyBotAI.cpp): a party
    // member mind controlled by a creature is not attacked (Commander::VetoChecks); the bot the
    // Commander sent to wake the last controlled enemy may attack it (IsAssignedToBreakControl).
    bool IsValidHostileTarget(Unit const* pTarget) const;
    // In combat: the Commander plan sends this bot to wake `pTarget`, the first of the
    // controlled enemies that are all that is left (Commander/Coordination/Doctrine/ControlledEnd.h).
    bool IsAssignedToBreakControl(Unit const* pTarget) const;

    std::vector<RaidTargetIcon> m_marksToFocus;
    // Healer damage tier the user set (`.partybot healdps 0-3`), -1 = automatic (Commander
    // Coordination/Doctrine/HealerDamage.h). Not saved: the addon sends it again, like the role.
    int8 m_healerDamageTier = -1;
    ShortTimeTracker m_updateTimer;
    bool m_resetSpellData = false;
    // The role was chosen by the user (.partybot setrole / load / add with a role, addbot with a
    // role); false = auto, the Commander party assignment may change it.
    bool m_roleLocked = false;
    // The role before SetEmergencyRole, ROLE_INVALID = no emergency role.
    CombatBotRoles m_roleBeforeEmergency = ROLE_INVALID;
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
    // Commander drop to the ground after a teleport (Commander/Behaviors/Movement/Landing.h),
    // every frame in UpdateAI.
    Commander::LandingState m_landing;
    // Dead: the corpse run (Commander/Behaviors/Support/CorpseRun.h); back alive: the walk to
    // the leader (Commander/Behaviors/Movement/Arrival.h).
    Commander::CorpseRunState m_corpseRun;
    Commander::ArrivalState m_arrival;
    // Out of combat: the walk to a dead member out of range or sight of the resurrection
    // (Commander/Behaviors/Support/ResurrectApproach.h).
    Commander::ResurrectApproachState m_resurrectApproach;
    // Out of combat: the leader walked away from its rest, it catches up before sitting again
    // (Commander/Behaviors/Movement/RestCatchUp.h; updated by the subclass that follows a leader).
    Commander::RestCatchUpState m_restCatchUp;
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
