/*
 * PartyBot Commander layer 3: threat concern (step 3), scored as benefit - cost
 * (value-scoring doc 5.3, 5.4).
 * Design: .kiro/docs/2026-11-01-threat-behavior.md
 *
 *  Target switches (2026-11-07): winning a loose enemy back with threat, topping up the
 *            smallest lead and a damage dealer's switch to a safe enemy moved to the brain
 *            (Coordination/Duties/TargetAssigner.h); the losses come from Perception/Values/LooseEnemy.h and
 *            the stop rule from Perception/Forecast/ThreatRoom.h, shared with it. This module keeps the
 *            taunts, area threat, stop hitting (HoldFire), Fade and pets.
 *  Team rescue (2026-11-07, todo 20): taking ONE loose enemy off its victim (taunt, winning it
 *            back with threat, the victim's Fade) is the brain's (Coordination/Duties/RescueAssigner.h,
 *            values in Perception/Values/RescueValue.h); RescueBehavior casts the taunt and Fade and
 *            takes Takeover::Taunt / ThreatDrop over. This module no longer taunts single
 *            enemies or fades.
 *  Tank    - area threat on several loose or thinly held enemies around it. Loose enemies the
 *            brain gave someone else (the priest fades it) are not counted. Taunts without
 *            SPELL_EFFECT_ATTACK_ME (Challenging Shout / Roar) only lend threat while their
 *            aura lasts: their loss also counts the hits after it fades.
 *            Melee enemies on a healer or ranged bot also cost the victim's output while they
 *            chase it (Value::HarassLoss; casters standing back do not).
 *            Area threat: threat per enemy from spell data (spell_threat + damage x multiplier,
 *            caster modifiers); a warrior spell in the wrong stance is cast after a stance swap
 *            (PartyBotAI::DoCastWithStance) and costs STANCE_SWAP_COST_SEC more. Damaging area
 *            spells are not proposed next to crowd controlled enemies.
 *  Damage  - near the holder's threat: stop hitting, or switch to a safe target, when the
 *            hits taken after pulling aggro outweigh the damage lost. Holder = tank bot, the
 *            player, or a tank-like ally (WorldModel::allies, AllyFacts::tankLike).
 *  Allies  - an enemy hitting an ally (tank-like or not) is never "loose" for the tank: it
 *            is not taunted or won back (combat scenario 9, Sergeant Bly).
 *  Healer  - if the heal the rotation would cast pulls aggro: cast a smaller safe heal; and
 *            either heal another member below OTHER_HEAL_PCT with a safe heal, or (nobody
 *            else needs it / no safe heal) veto healing that target this tick. The veto makes
 *            the rotation heal nobody this tick, hence the other-member option first.
 *            The other-member heal follows the mana budget (ManaBudgetBehavior::ComputeBudget):
 *            under tension > 1, only members below ManaBudget::HealCeilingPct. Heal ranks
 *            come from CombatBotBaseAI::ChooseHeal (HealSpells.h), the rotation's own choice.
 *            Targets below HEAL_IGNORE_THREAT_PCT are healed regardless.
 *  Pets    - of hunter / warlock bots (threat-behavior doc 4.7). Tank in the party (alive tank
 *            bot or tank-like ally): taunt autocast (Growl, Torment, Suffering) off, else on.
 *            No tank and the pet has a taunt: it is a holder (damage dealers leave room under
 *            it) and is left alone. Otherwise, near the holder's threat (same rule as damage
 *            dealers): attack a safe enemy (the tank's target first) or come back (PetFollow).
 *            Holding an enemy itself while a tank bot is there: switch to a safe enemy only
 *            (running back would bring the enemy to the owner). The tank treats enemies on
 *            pets as loose (taunt / win back, valued like any member).
 */

#ifndef MANGOS_COMMANDER_THREAT_BEHAVIOR_H
#define MANGOS_COMMANDER_THREAT_BEHAVIOR_H

#include "Commander/Behaviors/IBehavior.h"
#include "Commander/Perception/Forecast/ThreatRoom.h"

class Unit;
class SpellEntry;

namespace Commander
{
    struct EnemyFacts;
    struct PartyMemberFacts;

    class ThreatBehavior : public IBehavior
    {
    public:
        // Tunables (user approved 2026-11-01, see design doc section 2 / 3).
        static constexpr float DPS_STOP_HEADROOM = ThreatRoom::STOP_SHARE;     // x holder's threat
        static constexpr float DPS_SWITCH_HEADROOM = 0.25f;   // alternative target needs this much
        static constexpr float DPS_SWITCH_RANGE = 30.0f;
        static constexpr float DPS_SWITCH_MIN_SEC = 3.0f;     // switch target needs room for this much of my threat
        // An enemy seen for the first time less than this long ago (a summoned add, a patrol
        // that walked in) must leave room for NEW_ENEMY_LEAD_SEC of my threat before I hit it.
        static constexpr float NEW_ENEMY_SETTLE_SEC = ThreatRoom::NEW_ENEMY_SETTLE_SEC;
        static constexpr float NEW_ENEMY_LEAD_SEC = ThreatRoom::NEW_ENEMY_LEAD_SEC;
        static constexpr float HEAL_IGNORE_THREAT_PCT = 30.0f;
        // Instead of vetoing a heal that pulls aggro, heal another member below this directly.
        static constexpr float OTHER_HEAL_PCT = 70.0f;

        // Value scoring (value-scoring doc 5.3, 5.4).
        static constexpr float LOOSE_HORIZON_SEC = 10.0f;     // "do nothing" is judged over this long at most
        static constexpr float TAUNT_RESERVE_SEC = 1.0f;      // taunt cooldown cost: this many seconds of enemy melee
        static constexpr float TANK_RECLAIM_SEC = 2.0f;       // how long an enemy hits a dps/healer who pulled it
        static constexpr float SWITCH_TARGET_SEC = 0.5f;      // lost time when a dps switches target

        // Tank reach: loose enemies further than this or out of sight are another fight
        // (a teleported member's room), not something to run after.
        static constexpr float TANK_REACH_RANGE = 40.0f;
        // Several tank bots: a loose enemy is left to another tank bot that is at least this much nearer.
        static constexpr float TANK_SHARE_MARGIN = 2.0f;
        static constexpr float MELEE_REACH = 5.0f;
        static constexpr float RUN_YARDS_PER_SEC = 7.0f;
        // Area threat (Thunder Clap, Consecration, Swipe, Challenging Roar) when at least this
        // many loose or thinly held enemies are within GROUP_THREAT_RADIUS of the tank.
        static constexpr size_t GROUP_THREAT_MIN_ENEMIES = 2;
        static constexpr float GROUP_THREAT_RADIUS = 8.0f;
        static constexpr float GROUP_TAUNT_RESERVE_MULT = 3.0f; // area taunt cooldown (10 min) is worth more
        // A warrior cast that needs another stance (Thunder Clap from Defensive Stance, Taunt from
        // Battle Stance; PartyBotAI::DoCastWithStance) also costs this long of the tank's action:
        // weaker threat and armor in the other stance until the rotation swaps back.
        static constexpr float STANCE_SWAP_COST_SEC = 1.0f;
        char const* GetId() const override { return "threat"; }
        bool AppliesTo(PartyBotAI const* bot) const override;
        void Propose(PartyBotAI const* bot, WorldModel const& world,
                     TeamPlan const& plan, std::vector<Proposal>& out) const override;

        // Map thread. Points lost to enemies that turn on the healer `bot` when it heals `target`
        // with `spell` now (0 = safe). `effectiveOut`: the direct heal capped at the health
        // missing when it lands. Used by the healer branch here and by HealPlanBehavior.
        static float HealPullLoss(PartyBotAI const* bot, WorldModel const& world, Unit* target,
                                  SpellEntry const* spell, float* effectiveOut = nullptr);

    private:
        void ProposeTank(PartyBotAI const* bot, WorldModel const& world, TeamPlan const& plan,
                         std::vector<Proposal>& out) const;
        void ProposeDamage(PartyBotAI const* bot, WorldModel const& world, TeamPlan const& plan,
                           std::vector<Proposal>& out) const;
        void ProposeHealer(PartyBotAI const* bot, WorldModel const& world, std::vector<Proposal>& out) const;
        // The bot's own pet (any role): taunt autocast, and backing off a holder's enemy.
        void ProposePet(PartyBotAI const* bot, WorldModel const& world, std::vector<Proposal>& out) const;
        // Headroom `who` must keep on `enemy` (damage dealers and pets): DPS_STOP_HEADROOM of the
        // holder's threat, and its own threat for one bot tick (NEW_ENEMY_LEAD_SEC on new enemies).
        float NeededHeadroom(WorldModel const& world, ObjectGuid who, EnemyFacts const& enemy) const;
    };
}

#endif
