/*
 * PartyBot Commander layer 2: which paladin bot gives whom which blessing, and which party aura
 * each keeps up ("blessings" assigner, user 2026-11-15). Until then every paladin picked one
 * blessing and one aura at random (PopulateSpellData) and gave that blessing to everyone.
 *
 * Auras: each paladin bot a different one, the first of AURA_ORDER it knows that nobody has
 * yet: Devotion (armor), Concentration, Retribution, then Sanctity and the resistances. The aura
 * a player paladin keeps up itself counts as taken; one that keeps none is not counted on.
 *
 * Blessings, per member (players, not pets), the list Order() gives (user 2026-11-16):
 *   tank:    Light, Kings, Might, Sanctuary (no Salvation: it lowers threat)
 *   warlock: Salvation, Kings, Light, Wisdom, Sanctuary (its threat first)
 *   others:  Wisdom (uses mana) or Might (warrior, rogue; a paladin, shaman or druid in melee),
 *            Kings, Salvation, Light, Sanctuary
 * Each paladin gives a member one blessing (one per paladin per target in this core, a second
 * one replaces the first). Every order of the paladins is tried (up to MAX_PERMUTED of them,
 * else guid order) and the one giving the higher places wins, a higher place before any number
 * of lower ones (ties: guid order). So the paladin with Kings gives Kings and the other one
 * Wisdom. Blessings from anyone but these bots (a player paladin, a paladin outside the party)
 * count as taken.
 *
 * Greater Blessings (learned from 52 on) land on every group member of the target's class, so
 * the deal is first made per class, for its first member that is not a tank. Then per member,
 * preferring (at the same score) to keep each paladin's class blessing: kept = the Greater one,
 * changed = the plain one, cast after the Greater (PaladinRotation). So in a class with a tank
 * and damage dealers (warriors) the paladin with Greater Salvation gives the tank a plain Light,
 * with one paladin as with several. Not covered: a paladin left without a blessing for the tank
 * (more paladins than the tank's list) still puts its class one on it.
 *
 * Not a Duties module: DutyAllocator gives a bot one primary duty, a paladin blesses everyone.
 * Runs every refresh (pure function of the world model); the rotation casts out of combat
 * (PaladinRotation), and picks with Order() on its own while the module is off.
 */

#ifndef MANGOS_COMMANDER_BLESSING_ASSIGNER_H
#define MANGOS_COMMANDER_BLESSING_ASSIGNER_H

#include "Commander/Coordination/IAssigner.h"
#include "Commander/Perception/Profiles/PaladinBuffs.h"
#include <vector>

namespace Commander
{
    class BlessingAssigner : public IAssigner
    {
    public:
        static constexpr size_t MAX_PERMUTED = 4;

        // The member's blessings, most wanted first (header). role: CombatBotRoles.
        static std::vector<BlessingKind> Order(uint8 classId, uint8 role);
        static std::vector<PaladinAuraKind> AuraOrder();

        char const* GetId() const override { return "blessings"; }
        AssignStage GetStage() const override { return AssignStage::Tasks; }
        void Assign(WorldModel const& world, TeamPlan& plan) const override;
        void Clear(TeamPlan& plan) const override;
    };
}

#endif
