/*
 * What a companion's friend traits (Traits.h) do, while a bot AI plays him: PartyBotAI forwards
 * the PlayerAI hooks here (core code asks them only for a player with an AI, so a human on the
 * character gets none of it). Numbers from the design doc (.kiro/docs/2026-11-21-companion-
 * friend-traits.md §4, §5; combat traits priced in talent points, §3).
 *
 * Map thread (the bot's own), except the bench (CompanionMgr::UpdateBench reads Wanderer).
 */

#ifndef MANGOS_COMPANIONS_AFFINITY_TRAIT_EFFECTS_H
#define MANGOS_COMPANIONS_AFFINITY_TRAIT_EFFECTS_H

#include "Common.h"
#include "Companions/Affinity/Traits.h"

class Player;
class Unit;
class SpellEntry;
struct ItemPrototype;

namespace Companions
{
    namespace TraitEffects
    {
        // Per bot (a member of its AI).
        struct State
        {
            Traits::Trait life = Traits::Trait::None;
            Traits::Trait combat = Traits::Trait::None;
            uint32 nextLookMs = 0;
            // The combat trait whose stat changes are on him (Tough, Constitution, Sturdy).
            Traits::Trait statsOn = Traits::Trait::None;
            uint32 statsSchool = 0;
            bool swimSpeedOn = false;           // Swimmer: swim speed recomputed
            // Fights (Diligent, Breather).
            bool inCombat = false;
            uint32 combatSinceMs = 0;
            uint32 lastFightMs = 0;
            uint32 leftCombatMs = 0;
            // LastStand: the shield left and until when; ready again at.
            uint32 lastStandReadyMs = 0;
            uint32 shieldLeft = 0;
            uint32 shieldUntilMs = 0;
            uint32 guardReadyMs = 0;            // Guard
            // BackToWall: dead members seen, on until, ready again at.
            uint32 deadMembers = 0;
            uint32 backToWallUntilMs = 0;
            uint32 backToWallReadyMs = 0;
            // Rooted: where he stands still since when, armour on.
            float stillX = 0.0f, stillY = 0.0f;
            uint32 stillSinceMs = 0;
            bool armorOn = false;
            // RainLover: his zone's weather, looked at every few seconds.
            bool rainy = false;
            uint32 nextWeatherMs = 0;
        };

        // Every bot tick (cheap): his traits from CompanionMgr, stat changes on / off, fights.
        void Update(Player* me, State& state);

        void ChangeDamageTaken(Player* me, State& state, uint32& damage, SpellEntry const* spell);
        void ChangeHealingTaken(Player const* me, State const& state, uint32& heal);
        void ChangeAuraDuration(State const& state, SpellEntry const* spell, bool positive, int32& duration);
        void ChangeResurrection(Player const* me, State const& state, uint32& health, uint32& mana);
        float RegenRate(State const& state);
        float CastTimeRate(State const& state, SpellEntry const* spell);
        float PushbackRate(State const& state);
        float LockoutRate(State const& state);
        float FallDamageRate(State const& state);
        float BreathRate(State const& state);
        float SwimSpeedRate(State const& state);
        float ReputationRate(State const& state);
        float RepairCostRate(State const& state);
        float SellPriceRate(State const& state, ItemPrototype const* proto);
        float BuyPriceRate(State const& state, ItemPrototype const* proto);
        float LootMoneyRate(State const& state);
        float DurabilityLossRate(State const& state);
        float DetectedRangeRate(State const& state);
        int32 StealthDetectBonus(State const& state);
        float CraftExtraChance(State const& state, SpellEntry const* spell);
        uint32 SavedReagent(State const& state, SpellEntry const* spell);
        float GatherExtraChance(State const& state);

        // Travel when called over (Life/Errands/Travel): time rate and whether he skips the ship.
        float TravelTimeRate(State const& state);
        bool SkipsSailing(State const& state);
    }
}

#endif
