/*
 * Friend traits (挚友特质): at affinity LIFE_AT a companion rolls one life trait, at COMBAT_AT one
 * combat trait; rolled once and kept (custom_companion.trait_life / trait_combat, the key). Pools,
 * rarities and numbers decided with the user 2026-11-21, design
 * .kiro/docs/2026-11-21-companion-friend-traits.md (workspace root). What each trait does:
 * TraitEffects (only while a bot AI plays him: a human on the character has no AI).
 *
 * Adding a trait: an enum value and a row. `ready` only once its effect is wired (TraitEffects):
 * a trait that is not ready is never rolled nor shown, so nobody gets one that does nothing.
 */

#ifndef MANGOS_COMPANIONS_AFFINITY_TRAITS_H
#define MANGOS_COMPANIONS_AFFINITY_TRAITS_H

#include "Common.h"
#include <string>
#include <vector>

namespace Companions
{
    namespace Traits
    {
        uint32 const LIFE_AT = 80;
        uint32 const COMBAT_AT = 90;

        enum class Tier : uint8 { Life = 0, Combat = 1 };
        enum class Rarity : uint8 { Common = 0, Rare = 1, Epic = 2 };    // 普通 稀有 罕见

        enum class Trait : uint8
        {
            None = 0,
            // life (80)
            Diligent, NightOwl, EarlyBird, RainLover, Appetite, Frugal, Thrifty, Careful,
            HardWorker, Light, Swimmer, QuickHands,
            Crafty, Saver, Gatherer, Pickup, Scrounger, Likeable, Pathfinder, Wanderer,
            Quiet, SharpEye, Swift, Veteran,
            // combat (90)
            ThickSkin, Constitution, Revive, PoisonResist, Sturdy, Steady, Brave, Hardhead,
            SureFoot, LastStand, Breather, SelfCare,
            Tough, Composed, Desperate, BackToWall, Wary, Rooted, Dodgy, Alert,
            Focus, Guard, Counter, Grit,
        };

        struct Info
        {
            Trait trait;
            Tier tier;
            Rarity rarity;
            char const* key;        // stored, phrase "affinity.trait.<key>", addon id
            char const* tag;        // personality tag (Personality.h) that makes it 3x likelier, null = none
            uint32 classMask;       // 0 = any, else 1 << (class - 1)
            bool ready;             // its effect is wired (TraitEffects)
            char const* label;      // what the player reads (addon 好感 page)
            char const* text;       // its line there: what it does, no numbers (user's rule)
        };

        std::vector<Info> const& All();
        Info const* Of(Trait trait);
        Info const* Find(std::string const& key);
        uint32 TierAt(Tier tier);

        // The trait a companion gets at `tier`: rarity by weight (Common 60, Rare 30, Epic 10, among
        // the rarities that have a ready trait for his class), then a trait of it (his personality
        // tags 3x). The same for the same character. None: no ready trait for him.
        Trait Roll(uint32 charGuid, Tier tier, uint8 classId, std::vector<std::string> const& tags);

        // Constitution's school (SpellSchools, fire .. arcane), the same for the same character.
        uint32 ConstitutionSchool(uint32 charGuid);
        // Its line for this character (Constitution names its school).
        std::string TextFor(Info const& info, uint32 charGuid);
    }
}

#endif
