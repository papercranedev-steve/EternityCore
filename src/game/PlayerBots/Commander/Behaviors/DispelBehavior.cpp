#include "Commander/Behaviors/DispelBehavior.h"
#include "Commander/Classes/ClassKit.h"
#include "Commander/Perception/WorldModel.h"
#include "Commander/Perception/ProfileStore.h"
#include "Commander/Perception/ValueModel.h"
#include "PartyBotAI.h"
#include "Creature.h"
#include "Map.h"
#include "SpellAuras.h"
#include "SpellEntry.h"
#include <algorithm>

namespace Commander
{
    namespace
    {
        // Seconds until the caster puts the spell back after a dispel.
        float ReapplySec(CreatureSpellRef const& ref, Creature const* caster)
        {
            if (ref.source == SpellSource::OnHit)
            {
                float const attackSec = caster->GetAttackTime(BASE_ATTACK) / 1000.0f;
                if (attackSec <= 0.0f)
                    return DispelBehavior::DEFAULT_REAPPLY_SEC;
                float const chance = (ref.procChance > 0 && ref.procChance <= 100) ? ref.procChance / 100.0f : 1.0f;
                return attackSec / chance;
            }
            if (ref.repeatMaxMs)
                return (ref.repeatMinMs + ref.repeatMaxMs) / 2000.0f;
            return DispelBehavior::DEFAULT_REAPPLY_SEC;
        }

        bool IsHitting(Creature const* caster, Unit const* member)
        {
            return caster->GetVictim() == member || caster->CanReachWithMeleeAutoAttack(member);
        }

        // Every aura effect of the spell only slows movement (Frostbolt's chill, Hamstring);
        // direct damage effects already landed and do not count.
        bool OnlySlows(SpellEntry const* spell)
        {
            bool any = false;
            for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
            {
                if (spell->Effect[i] != SPELL_EFFECT_APPLY_AURA)
                    continue;
                if (spell->EffectApplyAuraName[i] != SPELL_AURA_MOD_DECREASE_SPEED)
                    return false;
                any = true;
            }
            return any;
        }

        // A healer or ranged member with a melee enemy on it has to walk away (escape module):
        // a slow on it matters. Tanks and melee stand at the enemy anyway.
        bool NeedsToMove(WorldModel const& world, PartyMemberFacts const& m)
        {
            return (m.role == ROLE_HEALER || m.role == ROLE_RANGE_DPS) && Value::IncomingOn(world, m.guid) > 0.0f;
        }
    }

    bool DispelBehavior::AppliesTo(PartyBotAI const* bot) const
    {
        return bot->Kit().CanDispel();
    }

    void DispelBehavior::Propose(PartyBotAI const* bot, WorldModel const& world,
                                 TeamPlan const& /*plan*/, std::vector<Proposal>& out) const
    {
        Player* me = bot->me;
        Map* map = me->GetMap();
        PartyMemberFacts const* self = world.FindMember(me->GetObjectGuid());
        if (!self)
            return;

        // Cost of one dispel: a global cooldown of this bot's normal work.
        float const dispelCost = Value::LostAction(world, *self, Value::GCD_SEC);

        for (PartyMemberFacts const& m : world.party)
        {
            if (m.isPet || !m.alive)
                continue;
            Unit* member = map->GetUnit(m.guid);
            if (!member)
                continue;

            for (auto const& itr : member->GetSpellAuraHolderMap())
            {
                SpellAuraHolder const* holder = itr.second;
                SpellEntry const* spell = holder->GetSpellProto();
                if (holder->IsPositive() || !spell->Dispel || !holder->GetCasterGuid().IsCreature())
                    continue;

                // Sleep that breaks on damage, on someone being hit: the next hit wakes them,
                // a dispel only spends a global cooldown (combat scenario 3, sleep on the tank).
                if (std::shared_ptr<SpellProfile const> sp = sProfileStore.GetSpell(spell->Id))
                {
                    if (sp->controls && sp->breaksOnDamage && !(sp->hasOverride && sp->override.dispel != DispelPolicy::Auto) &&
                        Value::IncomingOn(world, m.guid) > 0.0f)
                    {
                        Proposal p;
                        p.kind = ProposalKind::Veto;
                        p.source = "dispel";
                        p.reason = "breaks on the next hit";
                        p.veto.type = VetoType::Dispel;
                        p.veto.target = m.guid;
                        p.veto.spellId = spell->Id;
                        p.SetValue(dispelCost, Value::LostAction(world, m, WAKE_ON_DAMAGE_SEC));
                        out.push_back(p);
                        continue;
                    }

                    // A slow on someone who does not have to walk: the dispel saves nothing
                    // (strat-20261102-012937: Dispel Magic twice on a chilled rogue at full health).
                    if (!sp->controls && !(sp->hasOverride && sp->override.dispel != DispelPolicy::Auto) &&
                        OnlySlows(spell) && !NeedsToMove(world, m))
                    {
                        Proposal p;
                        p.kind = ProposalKind::Veto;
                        p.source = "dispel";
                        p.reason = "only slows, no need to move";
                        p.veto.type = VetoType::Dispel;
                        p.veto.target = m.guid;
                        p.veto.spellId = spell->Id;
                        // Saves the global cooldown (kept above 0: the arbiter drops benefit <= cost).
                        p.SetValue(std::max(dispelCost, 0.01f), 0.0f);
                        out.push_back(p);
                        continue;
                    }
                }

                Creature* caster = map->GetCreature(holder->GetCasterGuid());
                if (!caster || !caster->IsAlive() || !IsHitting(caster, member))
                    continue;   // caster dead or away: dispelling it is worth it again

                std::shared_ptr<CreatureProfile const> creature = sProfileStore.GetCreature(caster->GetEntry());
                CreatureSpellRef const* ref = creature ? creature->FindSpell(spell->Id) : nullptr;
                if (!ref || !ref->reapplies)
                    continue;

                std::shared_ptr<SpellProfile const> profile = sProfileStore.GetSpell(spell->Id);
                if (!profile || profile->controls)
                    continue;   // dangerous: dispel as usual
                if (profile->hasOverride && profile->override.dispel != DispelPolicy::Auto)
                    continue;   // the hand-written policy decides (VetoChecks)

                // What dispelling saves: the debuff's effect until the caster puts it back.
                SpellAmounts const amounts = SpellProfiles::Estimate(spell, caster->GetLevel(), 0.0f);
                float const durationSec = std::max(1.0f, profile->durationMs / 1000.0f);
                float const remainingSec = std::max(0.0f, holder->GetAuraDuration() / 1000.0f);
                float const perSec = amounts.periodicDamage * holder->GetStackAmount() / durationSec;
                float const saved = Value::Points(world, perSec * std::min(remainingSec, ReapplySec(*ref, caster)));

                Proposal p;
                p.kind = ProposalKind::Veto;
                p.source = "dispel";
                p.reason = ref->source == SpellSource::OnHit ? "on-hit, reapplied" : "repeats, reapplied";
                p.veto.type = VetoType::Dispel;
                p.veto.target = m.guid;
                p.veto.spellId = spell->Id;
                // Benefit of the veto = the dispel we save; cost = what the debuff does meanwhile.
                p.SetValue(dispelCost, saved);
                out.push_back(p);
            }
        }
    }
}
