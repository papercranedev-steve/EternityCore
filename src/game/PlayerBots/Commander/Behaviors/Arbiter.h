/*
 * PartyBot Commander layer 3: picks one proposal and executes it.
 * Value-scored proposals with benefit <= cost are dropped. Veto proposals are
 * registered in BotDirectives (the original rotation keeps running). Of the
 * replace proposals, the highest tier wins, then the highest score inside the tier.
 * ProposalTier::Override (retreat) hides every other module's replace proposal.
 * If execution fails (spell not ready, target gone) the next one in the same tier
 * is tried. Every candidate is written to the bot's decision log.
 * The best HealOrder is handed to the rotation's heal step. It competes with the replace
 * proposals (a Replace skips the rotation, heal step included): a Replace in a lower tier, or
 * lower in score in the same tier, stands down so the rotation runs and casts the heal
 * (Attack / Backpedal let the rotation run anyway and stay). From strat-20261102-012937.
 * UseObject (gameobject target) returns true both while walking and on use, so the original
 * rotation and movement stay off until the module stops proposing it.
 * Two channels (2026-11-08, user: "倒退和吼叫可以同时进行"): moving (MoveTo, Backpedal, Chase)
 * and acting (Cast, Attack, HoldFire). After the winner above executes, the best proposal of
 * the other channel (its own top tier) executes too when the two go together: an instant cast
 * that needs no standing still (CurrentAction::NeedsStandingStill), no auto shot, and for a
 * spell that must face its target the bot already faces it; Attack / HoldFire with any walk
 * but a chase. A cast-time spell and a walk still compete as one (the CurrentAction costs
 * decide). Not with an Override winner (retreat) nor with Hold, Rest, Retreat, UseObject,
 * StopMove, StopChase. The rotation runs after them only if both let it.
 * Pet actions (PetAttack, PetFollow, PetStay, PetAutocastOn/Off) are executed besides the bot's own
 * action and never skip the rotation: every autocast toggle, plus the best pet command
 * (none during a retreat, which calls the pet back itself).
 */

#ifndef MANGOS_COMMANDER_ARBITER_H
#define MANGOS_COMMANDER_ARBITER_H

#include "Commander/Registry.h"
#include <string>

class PartyBotAI;
class Unit;

namespace Commander
{
    struct WorldModel;
    struct TeamPlan;

    class Arbiter
    {
    public:
        // Returns true when an action was executed; the caller then skips the
        // original target selection, movement and class rotation for this tick.
        static bool Run(PartyBotAI* bot, WorldModel const& world, TeamPlan const& plan,
                        BehaviorList const& behaviors);

        // How long a decision stays in force without being renewed, in ms. Fixed, not a number
        // of bot ticks: the arbiter does not run while the bot casts, and in the tick after a
        // cast the original code that reads them (follow spot, class reflexes) runs before the
        // arbiter renews them. With "3 ticks" a lower BotUpdateMs let a takeover run out in the
        // middle of a cast. The values are the old ones at the default 1000 ms.
        static uint32 const TAKEOVER_MS = 3000;
        // Vetoes expire this long after the module stops proposing them.
        static uint32 const VETO_MS = 3000;
        // Movement hold of a walk whose proposal gives no holdMs (UseObject).
        static uint32 const MOVEMENT_HOLD_MS = 2000;

        // UseObject walks to this many yards in front of the object (inside INTERACTION_DISTANCE).
        static constexpr float USE_APPROACH_YARDS = 2.5f;

    private:
        static bool Execute(PartyBotAI* bot, Proposal const& proposal);
        // UseObject: walk into INTERACTION_DISTANCE (movement held), then GameObject::Use(me).
        // True while walking or after the use; false if the object is gone or the bot is rooted.
        static bool ExecuteUseObject(PartyBotAI* bot, BotAction const& action);
        // PetAttack / PetFollow / PetStay / PetAutocastOn / PetAutocastOff on bot->me->GetPet().
        // `pTarget` = the live unit of action.target (null if empty).
        static bool ExecutePetAction(PartyBotAI* bot, BotAction const& action, Unit* pTarget);
        static std::string Describe(PartyBotAI const* bot, Proposal const& proposal);
    };
}

#endif
