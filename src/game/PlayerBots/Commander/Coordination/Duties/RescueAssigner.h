/*
 * PartyBot Commander layer 2 (brain): team rescue, first part (todo item 20, user 2026-11-07;
 * .kiro/docs/2026-11-07/2026-11-07-team-rescue.md). An enemy hitting a member who is not a tank
 * (RescueValue::LooseVictim) gets ONE way off its victim for the whole party, instead of the
 * tank taunting, the tank walking over and the priest fading at once, each on its own.
 * Duties module.
 *
 * Offer, while the party fights: one need per loose enemy (now = true, kind Rescue,
 * exclusive DutyExclusive::EnemyHandled: a single target control or an intercept of the same
 * enemy is an alternative, whichever is worth more is answered). Not for an enemy the party
 * stays off (PatrolGuardPlan::Avoids) or the player said to leave alone. Candidates
 * (Perception/Values/RescueValue.h), each offered when its net is positive, rank = net (+ STICKY_POINTS
 * for the bot and spell given it last refresh):
 *   every tank bot that can act within TANK_REACH yd: each ready taunt (spellId), and winning it
 *     back with threat (spellId 0). A walk that would pull an idle pack or finds no way
 *     (RouteRisk::Blocked) is not offered: only a taunt already in range;
 *   the victim itself when it is a bot with a ready threat drop (Fade, its spellId).
 * points = the best net; detail DETAIL_URGENT when the victim dies within the horizon (or,
 * fading, within RescueValue::FADE_URGENT_SEC).
 * Apply: plan.rescues (TeamPlan::RescueDuty, the way from the chosen spell).
 *
 * Waiting taunt (TauntWaitSec, user 2026-11-15): Taunt / Growl give the tank the victim's threat
 * when they land (Spell::EffectTaunt). An enemy running at a ranged damage dealer who pulled it
 * hits nobody until it gets there, and every bolt the caster lands meanwhile is copied too. So
 * the taunt waits, when all of these hold: the duty is not urgent; the victim is a ranged damage
 * dealer (role, players too; not a pet; not the area phase's caster, AreaRoomBehavior holds its
 * runners itself); the taunt copies threat (TauntAbility::holdSec 0) and is ready; the enemy
 * runs in melee (melee on, not crowd controlled); the tank has it in reach now. The wait =
 * min(arrival at the victim, the time it stays in the tank's reach, WAIT_MAX_SEC)
 * - WAIT_LEAD_SEC (perception/brain refresh, the bot tick and the spell batch before the taunt
 * lands), at least WAIT_MIN_SEC, and only when the victim is expected to add at least
 * WAIT_MIN_GAIN_SHARE of its threat by then (Perception/Forecast/TauntWindow.h). Recomputed each
 * refresh; RescueDuty::waitUntilMs. Melee and healer victims: at once (the hits on them are real).
 *
 * Who executes: RescueBehavior casts the taunt / Fade (and takes both over from the rotations;
 * a waiting taunt when its time is up, or earlier when live the enemy is about to reach its
 * victim or leave the reach); TargetAssigner gives the tank the enemy when it wins it back with
 * threat, must walk into taunt range first, or keeps a waiting taunt's enemy in reach
 * (TankTargets.cpp); EscapeBehavior lets the victim keep casting instead of running to the tank.
 *
 * Backup heals (player combat doc T3, user 2026-11-28): while no healer bot can act (a real
 * player heals and may not keep up, or nobody heals), a member about to die also gets the
 * spare healers' heals as candidates (hybrid bots that are not tanks, PartyMemberFacts::
 * spareHealRates): per bot its best SurvivalValue::SaveByHeal, offered when it lands before
 * the member dies and pays. Only the dying: no top-ups, no health thresholds. RescueBehavior
 * casts it like the other saves (a druid leaves its form first, CanCastWithStance).
 *
 * Not yet (doc section 5): a shield on the victim, the victim running to the tank
 * (EscapeBehavior still decides that by itself, it only makes the tank's walk shorter), and
 * "someone is about to die" (Shield Wall, Last Stand, Blessing of Protection, Lay on Hands).
 */

#ifndef MANGOS_COMMANDER_RESCUE_ASSIGNER_H
#define MANGOS_COMMANDER_RESCUE_ASSIGNER_H

#include "Commander/Coordination/IAssigner.h"

namespace Commander
{
    struct EnemyFacts;
    struct PartyMemberFacts;

    class RescueAssigner : public IAssigner
    {
    public:
        // Loose enemies further than this from a tank, or out of its sight, are another fight
        // (a teleported member's room): same as TankTargets::TANK_REACH.
        static constexpr float TANK_REACH = 40.0f;
        static constexpr float MELEE_REACH = 5.0f;
        static constexpr float STICKY_POINTS = 0.5f;
        static constexpr uint8 DETAIL_URGENT = 1;
        // Subtracted from a consumable's rank in a Save need: every spell candidate comes first.
        static constexpr float CONSUMABLE_RANK_OFFSET = 1.0e6f;
        // Its value (DutyCandidate::value) is capped this far below the least valuable spell
        // candidate's: the allocator spends a consumable only to free a spell bot for more.
        static constexpr float CONSUMABLE_VALUE_GAP = 0.1f;
        // Waiting taunt (header). Lead: a refresh (0.2 s) + a bot tick (0.2 s) + a spell batch
        // (0.4 s) between a decision on these facts and the taunt landing.
        static constexpr float WAIT_LEAD_SEC = 0.8f;
        static constexpr float WAIT_MIN_SEC = 0.3f;
        static constexpr float WAIT_MAX_SEC = 4.0f;
        static constexpr float WAIT_MIN_GAIN_SHARE = 0.05f;

        // Seconds `tank` waits before taunting `e` with `spellId` (0 = now); `gain` gets the
        // threat the wait is expected to copy. Shared with TankTargets (it runs before Apply).
        static float TauntWaitSec(WorldModel const& world, TeamPlan const& plan, EnemyFacts const& e,
                                  PartyMemberFacts const& tank, uint32 spellId, bool urgent, float* gain = nullptr);

        // Same id as RescueBehavior: `.partybot ai module rescue off` switches both.
        char const* GetId() const override { return "rescue"; }
        AssignStage GetStage() const override { return AssignStage::Duties; }
        void Offer(WorldModel const& world, TeamPlan const& plan, std::vector<DutyNeed>& out) const override;
        void Apply(WorldModel const& world, std::vector<DutyResult> const& results, TeamPlan& plan) const override;
        void Clear(TeamPlan& plan) const override;
    };
}

#endif
