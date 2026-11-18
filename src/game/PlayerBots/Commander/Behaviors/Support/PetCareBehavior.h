/*
 * PartyBot Commander layer 3 (cerebellum): the bot heals its own pet in combat ("petcare"
 * module, user 2026-11-15: the hunter's pet holds enemies for the party, so it must live).
 * Bots whose kit has ClassKit::PetHeal (hunter Mend Pet). Only this module casts it in combat;
 * out of combat the rotation does.
 *
 * Healers heal pets last (CombatBotBaseAI: only when no player is hurt) and a human healer may
 * not at all (other roles may fail, ai-architecture rule), so the owner keeps its pet up
 * itself; a healer bot healing it is counted in its danger (SurvivalValue::Of).
 *
 * Mend Pet, in combat:
 *   gates    pet alive, controlled, hurt, not already mended; the cast possible (range, mana).
 *   heal     the heal's ticks (HealSpells::Estimate, bonuses included) over its channel d,
 *            capped at the health missing + the damage the pet takes during d.
 *   benefit  Points(heal) + DeathPenalty(pet) x (Risk(ttd) - Risk(ttd')): ttd its time to die
 *            now (SurvivalValue::Of), ttd' with the heal's rate added from its first tick.
 *   cost     LostAction(owner, d) (no shots while channelling) + ManaWorth::Points(its mana).
 * Tier Routine: the rotation's shots otherwise.
 */

#ifndef MANGOS_COMMANDER_PET_CARE_BEHAVIOR_H
#define MANGOS_COMMANDER_PET_CARE_BEHAVIOR_H

#include "Commander/Behaviors/IBehavior.h"

namespace Commander
{
    class PetCareBehavior : public IBehavior
    {
    public:
        char const* GetId() const override { return "petcare"; }
        bool AppliesTo(TeamBotAI const* bot) const override;
        void Propose(TeamBotAI const* bot, WorldModel const& world,
                     TeamPlan const& plan, std::vector<Proposal>& out) const override;
    };
}

#endif
