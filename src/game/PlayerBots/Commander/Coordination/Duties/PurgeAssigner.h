/*
 * PartyBot Commander layer 2: which bot takes which buff off an enemy ("purge" duties, boss
 * doc G11 .kiro/docs/2026-11-28-dungeon-boss-encounters.md). Removals: shaman Purge and
 * priest Dispel Magic (magic), hunter Tranquilizing Shot (enrage: Frenzy), ClassKit::Purges.
 *
 * In a fight (WorldModel::HasPartyEnemy), each buff of an acting enemy (EnemyFacts::buffs) is
 * worth what it adds until it ends or the enemy dies (Value::EnemyTimeToDie):
 *   melee boost  its victim a party member and its melee on: the extra melee damage per
 *                second (EnemyFacts::meleeDps x (1 - periodMult / damageMult) + damageFlat per
 *                swing), Value::Points over that time
 *   absorb       the fight it lengthens: Value::FightExtension(absorb / Value::DealtDps)
 * Other buffs are not valued (no need). A bot's cost: one global cooldown, the mana
 * (ManaWorth), the cooldown (Value::CooldownPoints: Tranquilizing Shot 20 s). Candidates:
 * bots with a ready removal of that dispel type within its range; the need is not urgent
 * (DutyNeed::now false: kicks and rescues come first). One need per enemy and buff
 * (DutyKind::Purge, target = the enemy, auraSpellId = the buff).
 *
 * PurgeBehavior casts it. Known limits: Purge and Dispel Magic take off a magic buff the
 * server picks, not always the one named; the enemy putting it back is not counted.
 */

#ifndef MANGOS_COMMANDER_PURGE_ASSIGNER_H
#define MANGOS_COMMANDER_PURGE_ASSIGNER_H

#include "Commander/Coordination/IAssigner.h"

namespace Commander
{
    struct EnemyFacts;
    struct EnemyBuffFacts;

    class PurgeAssigner : public IAssigner
    {
    public:
        // Points `buff` on `e` adds while it lasts (header).
        static float Worth(WorldModel const& world, EnemyFacts const& e, EnemyBuffFacts const& buff);
        // The bot that had it the last refresh keeps it unless another gains this much more.
        static constexpr float STICKY_SEC = 1.0f;

        char const* GetId() const override { return "purge"; }
        AssignStage GetStage() const override { return AssignStage::Duties; }
        void Offer(WorldModel const& world, TeamPlan const& plan, std::vector<DutyNeed>& out) const override;
        void Apply(WorldModel const& world, std::vector<DutyResult> const& results, TeamPlan& plan) const override;
        void Clear(TeamPlan& plan) const override;
    };
}

#endif
