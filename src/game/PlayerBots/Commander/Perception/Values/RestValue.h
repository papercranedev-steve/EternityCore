/*
 * PartyBot Commander layer 1 (eyes): what eating and drinking to full now is worth to a party
 * member who is not in combat (user, 2026-11-07: a caster sitting with a drink when the pull
 * starts keeps drinking while the tank's pressure is low, then heals or fights; the Commander
 * owns all resting since, Behaviors/Support/RestBehavior.h). Pure, on WorldModel facts.
 *
 * Game rules behind it: a member nobody has on a threat list is not in combat and may eat and
 * drink; a hit stands it up and puts it in combat, and in combat nobody eats or drinks. So mana
 * not drunk before its first action of the fight is not drunk until the fight ends.
 *
 * Three cases:
 *  - no fight and none planned (after a fight, travelling): resting costs nothing and nothing
 *    else is to be done; Worth::free, the behavior proposes it at Routine tier, unvalued.
 *  - a fight planned (a pull), none on yet: valued with the pull's FightLoad estimate.
 *  - the party fights: valued with the live rates; no eating (heals are the answer then).
 * Valued:
 *   seconds  the longer of: (maxMana - mana) / (drinkManaPerSec + manaRegen; manaRegen alone
 *            while drinking), (maxHealth - health) / foodHealthPerSec
 *   benefit  ManaWorth::Points of maxMana - mana (live fight, or `planned`; its scarcity makes
 *            the drink worth little while the mana is enough for the fight), plus the missing
 *            health in points when eating
 *   cost     healers 0: their heal orders compete with the drink every tick (an emergency heal
 *            is Urgent and comes first). Others: their part of the fight while sitting,
 *            Value::ContributionPerSecAt(incoming) x seconds.
 * Members without the rates (players, no pool for drinking): nothing to rest.
 * Drinking starts only below DrinkBelowShare (the next fight's mana, below), eating below
 * START_SHARE.
 */

#ifndef MANGOS_COMMANDER_REST_VALUE_H
#define MANGOS_COMMANDER_REST_VALUE_H

#include "Commander/Perception/Values/ManaWorth.h"
#include "Commander/Perception/Values/FightManaSpend.h"

namespace Commander
{
    struct WorldModel;
    struct PartyMemberFacts;

    namespace RestValue
    {
        // Mana and health at or above this share count as full (resting ends at 100%).
        static constexpr float FULL_SHARE = 0.99f;
        // A new food starts below this share only (user 2026-11-21: each sitting uses one up,
        // a sip for the last few percent wastes it); the one on keeps going to full. Also the
        // highest share a new drink starts below (DrinkStartShare).
        static constexpr float START_SHARE = 0.95f;

        // A new drink (user 2026-11-24: casters drank after every open-world creature): when
        // the mana left will not cover the next fight. Below FIGHTS_MARGIN x the average cost
        // of the last fights in this kind of place (Values/FightManaSpend.h), once
        // MIN_FIGHTS were recorded there; before that the place's default: open world
        // OPEN_WORLD_DRINK_SHARE, dungeons and raids DUNGEON_DRINK_SHARE (user: 50% / 90%).
        // Kept between MIN_DRINK_SHARE and START_SHARE. The drink on still runs to full.
        static constexpr float OPEN_WORLD_DRINK_SHARE = 0.50f;
        static constexpr float DUNGEON_DRINK_SHARE = 0.90f;
        static constexpr float FIGHTS_MARGIN = 1.5f;
        static constexpr uint32_t MIN_FIGHTS = 3;
        static constexpr float MIN_DRINK_SHARE = 0.30f;
        float DrinkStartShare(FightManaSpendState const& spend, bool dungeon);

        // The share below which `member` drinks now: to full while drinking, else the higher
        // of its own (PartyMemberFacts::drinkStartShare) and `pullReadyShare`, the mana a
        // pull waiting for the party wants from it (PullPlanner::ReadyManaShare; 0 = none):
        // the pull would wait for mana the bot did not drink otherwise.
        float DrinkBelowShare(PartyMemberFacts const& member, float pullReadyShare);

        struct Worth
        {
            bool eat = false;       // health below full, eating allowed now
            bool drink = false;     // mana below full
            bool free = false;      // no fight on or planned: rest, nothing else to do
            float sec = 0.0f;
            float mana = 0.0f;
            float health = 0.0f;
            float benefit = 0.0f;
            float cost = 0.0f;
            bool Any() const { return eat || drink; }
            bool Pays() const { return Any() && (free || benefit > cost); }
        };

        // `planned`: the pull's estimate (sec 0 = none), used while no damage comes in yet.
        // `pullReadyShare`: DrinkBelowShare's.
        Worth Of(WorldModel const& world, PartyMemberFacts const& member, ManaWorth::Fight const& planned,
                 float pullReadyShare);

        // Seconds eating and drinking from `health` and `mana` to full take: the longer of the
        // two (both run at once). Eating assumed allowed.
        float SecondsToFull(PartyMemberFacts const& member, float health, float mana);

        // Seconds until `member` has `mana` / `health` (absolute), sitting with its drink / food
        // from now (the one on, or a new one; spirit regeneration counted for mana; without a
        // drink spirit regeneration alone). 0 already there, < 0 never (nothing restores it).
        float SecondsToMana(PartyMemberFacts const& member, float mana);
        float SecondsToHealth(PartyMemberFacts const& member, float health);

        // Seconds of resting one health-to-mana cast (warlock Life Tap, tapManaPerCast) saves
        // before sitting down (user 2026-11-16: mana empty, health full -> tap until the two
        // take about as long, then eat and drink). 0 when it saves nothing: a fight on (no
        // eating, the health is not won back), no tap, its mana does not fit, the health does
        // not pay it, already eating or drinking (user 2026-11-26: it sat down in each tap's
        // global cooldown and stood up for the next tap, a drink thrown away each time).
        float TapSavesSec(WorldModel const& world, PartyMemberFacts const& member);

        // The reverse (user 2026-11-27, every class with a self-heal): seconds of resting one
        // heal of `heal` on itself for `mana` saves before sitting down (mana full, health low
        // -> heal until the two take about as long, then eat and drink together). The heal
        // counts up to the missing health. Same conditions as TapSavesSec; the caller weighs it
        // against the cast's busy time (max(cast, global cooldown)).
        float SelfHealSavesSec(WorldModel const& world, PartyMemberFacts const& member, float heal, float mana);
    }
}

#endif
