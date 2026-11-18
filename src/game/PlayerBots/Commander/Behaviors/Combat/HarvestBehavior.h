/*
 * PartyBot Commander layer 3 (cerebellum): an item from an enemy about to die ("harvest" module,
 * user 2026-11-15: the warlock's Soul Shards). Bots whose kit has ClassKit::DeathHarvest
 * (warlock Drain Soul: a Soul Shard when its target dies while it channels).
 *
 * Every tick while the party fights and the bot is free (the arbiter does not run while it
 * casts, so this is right after its own cast, its Shadow Bolt maybe still in the air):
 *   candidates  enemies that die within SHARD_WORTH_SEC of the others' damage and its own hit
 *               already on the way (FinishForecast::DeathMsOwnHitOnly), that give the item
 *               (the rules of Aura::HandleChannelDeathItem: Player::IsHonorOrXPTarget, tapped by
 *               the bot's party, not already carrying its own death item aura: a Shadowburn
 *               gives that shard), in sight, castable (TeamBotAI::CanTryToCastSpell: range,
 *               mana, the plan's constraints).
 *   not when    it holds KEEP of the item, or has no bag room for one.
 *   benefit     LostAction(SHARD_WORTH_SEC x (1 - held / KEEP)): one more item is worth some
 *               seconds of its own casting, less the more it holds
 *   cost        LostAction(seconds until it dies): it channels instead of its filler
 * The soonest to die wins; tier Routine (the rotation goes on otherwise).
 *
 * While the channel runs: ContinueValue = that benefit while its target still dies within
 * SHARD_WORTH_SEC, else 0; CastBreakBenefit = LostAction(time left) once it does not (healed,
 * the others switched away): the rest of the channel is casting lost.
 */

#ifndef MANGOS_COMMANDER_HARVEST_BEHAVIOR_H
#define MANGOS_COMMANDER_HARVEST_BEHAVIOR_H

#include "Commander/Behaviors/IBehavior.h"

namespace Commander
{
    class HarvestBehavior : public IBehavior
    {
    public:
        // User 2026-11-15: "5-10 to be sure there are enough". The rotation's Shadowburn keeps
        // WarlockRotation's reserve below this.
        static constexpr uint32 KEEP = 10;
        // Untested guess: an item when it holds none is worth this many seconds of its casting
        // (about two Shadow Bolts).
        static constexpr float SHARD_WORTH_SEC = 5.0f;

        char const* GetId() const override { return "harvest"; }
        bool AppliesTo(TeamBotAI const* bot) const override;
        void Propose(TeamBotAI const* bot, WorldModel const& world,
                     TeamPlan const& plan, std::vector<Proposal>& out) const override;
        float CastBreakBenefit(TeamBotAI const* bot, WorldModel const& world, TeamPlan const& plan,
                               uint32 castLeftMs, char const*& reason) const override;
        float ContinueValue(TeamBotAI const* bot, WorldModel const& world, TeamPlan const& plan,
                            SpellEntry const* spell, char const*& reason) const override;
    };
}

#endif
