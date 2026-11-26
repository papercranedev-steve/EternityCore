/*
 * PartyBot Commander layer 3: carries out the brain's area phase ("aoe" module).
 *
 * History: since 2026-11-02 the rotations cast no area spells (Rotations/MageRotation.cpp:
 * without the Commander the mage casts Frostbolt). Until 2026-11-08 this module also decided
 * when, which and where, each bot for itself: a Blizzard opened right after the tank's line of
 * sight hide, the tank was sent off a patrol's path in the middle of the channel, and 7 of 8
 * Blizzards since 2026-11-07 ended early (strat-20261108-161025). Now the brain decides
 * (Coordination/Doctrine/AreaPhase.h, TeamPlan::area) and the values come from one place
 * (Perception/Values/AreaValue.h). Design: .kiro/docs/2026-11-08-aoe-phase.md.
 *
 * Before its spell (its AreaPrep in TeamPlan::area preps, in combat or not; 2026-11-12): while
 *   the party holds for the pull or the opening, or it waits: walk to the first of its spots that
 *   passes the map checks below (value moveGain; cost moveCost in combat, 0 before;
 *   Hold while walking), then its shield (shieldId, net gain). Tier Assigned.
 * Propose (its AreaCast in TeamPlan::area casts, the caster in combat or not yet: the brain plans
 *   casts only in the party's fight; several casters may each have one):
 *   the healer named for a cast (allyShieldBot): its shield on that caster when not on it yet,
 *     tier Assigned, net allyShieldGain.
 *   the caster, in its cast's order:
 *     Move    walk to the first of `spots` (AreaSpot.h, best first) while farther than
 *             SPOT_REACHED_YD from all of them, when the spot passes the map
 *             checks (ground within MAX_STEP_Z of the caster, outside idle creatures' reach, out
 *             of hazards and patrol paths, line of sight to the centre, a safe path); not usable:
 *             cast from where it stands if netHere > 0. Value moveBenefit / moveCost.
 *     Shield  its own shield (Ice Barrier) when its aura is not on. Value shieldGain. Not
 *             castable this moment (GCD): nothing, the spell waits for it.
 *     Cast    the spell on the centre enemy (ground spell) or itself, when CanTryToCastSpell
 *             passes (range, mana, TeamBotAI::PassesPartyCastChecks). Value benefit / cost of
 *             the whole spell.
 *   Channel: nothing (the channel watch below).
 *
 * Channel watch (WatchChannel, every frame from PartyBotAI::UpdateAI, at most every
 * CHANNEL_CHECK_MS): while one of these spells is channelled, AreaValue on the ticks left
 * (paid: the mana is spent, going on saves the filler's mana) with the world model of the
 * party (refreshed by the brain), the tank the plan sends elsewhere and the caster's own
 * health under the enemies it turns. Cancelled only when every number of further ticks loses
 * points; the stop is written to the decision log ("aoe stop-channel"). The same value is what
 * ending the channel costs any other proposal (ContinueValue, Behaviors/CurrentAction.h).
 * Starting and going on use the same valuation, so a channel stopped for a reason is not
 * started again while the reason lasts (the start judges the whole spell and its mana).
 */

#ifndef MANGOS_COMMANDER_AREA_DAMAGE_BEHAVIOR_H
#define MANGOS_COMMANDER_AREA_DAMAGE_BEHAVIOR_H

#include "Commander/Behaviors/IBehavior.h"

class SpellEntry;

namespace Commander
{
    class AreaDamageBehavior : public IBehavior
    {
    public:
        static constexpr uint32 CHANNEL_CHECK_MS = 200;
        static constexpr float SPOT_REACHED_YD = 2.0f;
        static constexpr float MAX_STEP_Z = 4.0f;
        static constexpr float LOS_EYE_HEIGHT = 2.0f;
        static constexpr float MAX_PATH_DETOUR = 1.8f;
        static constexpr float RUN_YD_PER_SEC = 7.0f;
        static constexpr uint32 HOLD_EXTRA_MS = 500;

        char const* GetId() const override { return "aoe"; }
        bool AppliesTo(TeamBotAI const* bot) const override;
        void Propose(TeamBotAI const* bot, WorldModel const& world,
                     TeamPlan const& plan, std::vector<Proposal>& out) const override;

        // `spell` is one of the bot's ClassKit::AreaDamage spells and this module is on: its
        // threat is judged by the area phase, TeamBotAI::PassesPartyCastChecks
        // skips its rough check (which would block the module's own casts).
        static bool Judges(TeamBotAI const* bot, SpellEntry const* spell);

        // Every frame from PartyBotAI::UpdateAI (channel watch above). True = cancelled.
        static bool WatchChannel(TeamBotAI* bot, uint32& lastCheckMs);

        // The channel watch's best net of the further ticks (>= 0): what ending the bot's own
        // area channel costs any other proposal (Behaviors/CurrentAction.h).
        float ContinueValue(TeamBotAI const* bot, WorldModel const& world, TeamPlan const& plan,
                            SpellEntry const* spell, char const*& reason) const override;
    };
}

#endif
