#include "Commander/Behaviors/InterruptBehavior.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/WorldModel.h"
#include "Commander/Perception/SpellTags.h"
#include "PartyBotAI.h"
#include "Map.h"
#include "Spell.h"
#include "SpellMgr.h"
#include <algorithm>

namespace Commander
{
    namespace
    {
        // Live check on the map thread: the plan can be up to one perception interval old.
        bool IsCastingInterruptible(Unit const* enemy, float* progressOut, SpellEntry const** spellOut)
        {
            Spell const* spell = enemy->GetCurrentSpell(CURRENT_GENERIC_SPELL);
            bool channeled = false;
            if (!spell || spell->getState() == SPELL_STATE_FINISHED)
            {
                spell = enemy->GetCurrentSpell(CURRENT_CHANNELED_SPELL);
                channeled = true;
            }
            if (!spell || spell->getState() == SPELL_STATE_FINISHED)
                return false;
            if (!SpellTags::IsInterruptible(spell->m_spellInfo, channeled, spell->GetCastTime()))
                return false;

            int32 const total = channeled ? spell->m_spellInfo->GetDuration() : spell->GetCastTime();
            *progressOut = total > 0 ? 1.0f - float(spell->GetCastedTime()) / float(total) : 0.0f;
            *spellOut = spell->m_spellInfo;
            return true;
        }

        bool HasReadyInterrupt(PartyMemberFacts const* m)
        {
            if (!m || !m->alive || m->casting)
                return false;
            return std::any_of(m->interrupts.begin(), m->interrupts.end(),
                [](InterruptAbility const& a) { return a.ready; });
        }
    }

    bool InterruptBehavior::AppliesTo(PartyBotAI const* bot) const
    {
        switch (bot->me->GetClass())
        {
            case CLASS_WARRIOR:
            case CLASS_ROGUE:
            case CLASS_MAGE:
            case CLASS_SHAMAN:
            case CLASS_DRUID:
            case CLASS_PALADIN:
                return true;
            default:
                return false;
        }
    }

    void InterruptBehavior::Propose(PartyBotAI const* bot, WorldModel const& world,
                                    TeamPlan const& plan, std::vector<Proposal>& out) const
    {
        Player* me = bot->me;
        ObjectGuid const self = me->GetObjectGuid();
        PartyMemberFacts const* myFacts = world.FindMember(self);
        if (!myFacts)
            return;

        for (InterruptDuty const& duty : plan.interrupts)
        {
            bool const isPrimary = duty.primary == self;
            bool const isBackup = duty.backup == self;
            if (!isPrimary && !isBackup)
                continue;

            Unit* enemy = me->GetMap()->GetUnit(duty.enemy);
            if (!enemy || !enemy->IsAlive())
                continue;

            float progress = 0.0f;
            SpellEntry const* cast = nullptr;
            if (!IsCastingInterruptible(enemy, &progress, &cast))
                continue;

            if (!isPrimary)
            {
                bool const primaryUnable = duty.primary.IsEmpty() || !HasReadyInterrupt(world.FindMember(duty.primary));
                if (!primaryUnable && progress < BACKUP_AFTER_PROGRESS)
                    continue;
            }

            // Heals and crowd control on us are as urgent as a taunt; damage casts are a team task.
            CastCategory const category = SpellTags::Categorize(cast);
            ProposalTier const tier = (category == CastCategory::Heal || category == CastCategory::Control)
                ? ProposalTier::Urgent : ProposalTier::Assigned;
            float const base = 5.0f + float(uint8(category)) + (isPrimary ? 0.5f : 0.0f);

            // Every ready ability, assigned one first, then cheaper cooldowns: the arbiter tries
            // them in order if one fails (out of range, immune, wrong form).
            std::vector<InterruptAbility> abilities = myFacts->interrupts;
            uint32 const assigned = isPrimary ? duty.primarySpellId : duty.backupSpellId;
            std::stable_sort(abilities.begin(), abilities.end(), [assigned](InterruptAbility const& a, InterruptAbility const& b)
            {
                if ((a.spellId == assigned) != (b.spellId == assigned))
                    return a.spellId == assigned;
                return a.cooldownMs < b.cooldownMs;
            });

            float step = 0.0f;
            bool anyMelee = false;
            for (InterruptAbility const& a : abilities)
            {
                if (!a.ready)
                    continue;
                SpellEntry const* spell = sSpellMgr.GetSpellEntry(a.spellId);
                if (!spell)
                    continue;
                anyMelee = anyMelee || a.maxRange <= MELEE_ABILITY_RANGE;

                Proposal p;
                p.tier = tier;
                p.score = base - step;
                p.source = "interrupt";
                p.action.type = BotActionType::Cast;
                p.action.target = duty.enemy;
                p.action.spell = spell;
                out.push_back(p);
                step += 0.01f;
            }

            // Melee primary not in reach: go to the caster (original chase logic moves us).
            if (isPrimary && anyMelee && bot->GetRole() != ROLE_TANK && me->GetVictim() != enemy &&
                !me->CanReachWithMeleeAutoAttack(enemy))
            {
                Proposal p;
                p.tier = tier;
                p.score = base - 1.0f;
                p.source = "interrupt";
                p.action.type = BotActionType::Attack;
                p.action.target = duty.enemy;
                out.push_back(p);
            }
        }
    }
}
