#include "Commander/Behaviors/Support/PetCareBehavior.h"
#include "Commander/Classes/ClassKit.h"
#include "Commander/Perception/Values/ManaWorth.h"
#include "Commander/Perception/Values/SurvivalValue.h"
#include "Commander/Perception/Values/ValueModel.h"
#include "Commander/Perception/WorldModel.h"
#include "TeamBotAI.h"
#include "HealSpells.h"
#include "Pet.h"
#include "SpellEntry.h"
#include <algorithm>

namespace Commander
{
    bool PetCareBehavior::AppliesTo(TeamBotAI const* bot) const
    {
        return bot->Kit().PetHeal() != nullptr;
    }

    void PetCareBehavior::Propose(TeamBotAI const* bot, WorldModel const& world,
                                  TeamPlan const& /*plan*/, std::vector<Proposal>& out) const
    {
        Player* me = bot->me;
        SpellEntry const* heal = bot->Kit().PetHeal();
        if (!heal || !me->IsInCombat())
            return;
        Pet* pet = me->GetPet();
        if (!pet || !pet->IsAlive() || !pet->IsControlled() || pet->IsCharmed() || pet->HasAura(heal->Id))
            return;
        PartyMemberFacts const* self = world.FindMember(me->GetObjectGuid());
        PartyMemberFacts const* petFacts = world.FindMember(pet->GetObjectGuid());
        if (!self || !petFacts || !self->CanAct() || self->silenced || petFacts->health >= petFacts->maxHealth)
            return;
        if (!bot->CanTryToCastSpell(pet, heal))
            return;

        HealSpells::Amount const amount = HealSpells::Estimate(me, pet, heal);
        float const channelSec = amount.periodicSec;
        if (amount.periodic <= 0.0f || channelSec <= 0.0f)
            return;

        SurvivalValue::Danger const danger = SurvivalValue::Of(world, *petFacts);
        float const missing = float(petFacts->maxHealth - petFacts->health);
        float const healed = std::min(amount.periodic, missing + danger.dps * channelSec);

        // Time to die with the heal's rate on top of the healers', from its first tick on.
        float ttdAfter = danger.ttd;
        if (danger.dps > 0.0f || !danger.timeline.events.empty())
        {
            float tickSec = channelSec;
            for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
                if (heal->EffectApplyAuraName[i] == SPELL_AURA_PERIODIC_HEAL && heal->EffectAmplitude[i] > 0)
                    tickSec = std::min(tickSec, float(heal->EffectAmplitude[i]) / 1000.0f);
            // The same hits as danger.ttd (IncomingTimeline), the Mend Pet rate on top.
            IncomingTimeline::Relief relief = danger.Healers();
            relief.healPerSec += amount.periodic / channelSec;
            relief.healDelaySec = danger.heal > 0.0f ? std::min(danger.healDelaySec, tickSec) : tickSec;
            ttdAfter = danger.timeline.TimeToLose(float(petFacts->health) + petFacts->absorb, false, relief);
        }
        float const saved = std::max(0.0f, SurvivalValue::Risk(danger.ttd) - SurvivalValue::Risk(ttdAfter));

        Proposal p;
        p.tier = ProposalTier::Routine;
        p.source = "petcare";
        p.reason = danger.InDanger() ? "pet about to die: mend it" : "pet hurt: mend it";
        p.action.type = BotActionType::Cast;
        p.action.spell = heal;
        p.action.target = pet->GetObjectGuid();
        p.SetValue(Value::Points(world, healed) + Value::DeathPenalty(*petFacts) * saved,
                   Value::LostAction(world, *self, channelSec) +
                   ManaWorth::Points(world, *self, HealSpells::ManaCost(me, heal)));
        out.push_back(p);
    }
}
