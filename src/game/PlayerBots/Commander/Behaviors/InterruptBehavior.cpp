#include "Commander/Behaviors/InterruptBehavior.h"
#include "Commander/Classes/ClassKit.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/WorldModel.h"
#include "Commander/Perception/SpellTags.h"
#include "Commander/Perception/CastValue.h"
#include "PartyBotAI.h"
#include "Map.h"
#include "Spell.h"
#include "SpellMgr.h"
#include <algorithm>

namespace Commander
{
    namespace
    {
        struct LiveCast
        {
            SpellEntry const* spell = nullptr;
            ObjectGuid target;
            float progress = 0.0f;
            uint32 remainingMs = 0;
            uint32 totalMs = 0;
        };

        // Live check on the map thread: the plan can be up to one perception interval old.
        bool IsCastingInterruptible(Unit const* enemy, LiveCast& out)
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
            out.progress = total > 0 ? 1.0f - float(spell->GetCastedTime()) / float(total) : 0.0f;
            out.remainingMs = uint32(spell->GetCastedTime());     // m_timer counts down to 0
            out.totalMs = uint32(std::max(0, total));
            out.spell = spell->m_spellInfo;
            out.target = spell->m_targets.getUnitTargetGuid();
            return true;
        }

        // The perceived facts of the enemy with its live cast filled in, so benefit, lockout
        // and cooldown cost all see the cast that is running now.
        EnemyFacts LiveFacts(EnemyFacts const& facts, LiveCast const& cast)
        {
            EnemyFacts e = facts;
            e.castingSpellId = cast.spell->Id;
            e.castTarget = cast.target;
            e.castInterruptible = true;
            e.castRemainingMs = cast.remainingMs;
            e.castTotalMs = cast.totalMs;
            return e;
        }

        // Can act (not asleep / stunned / charmed), not busy, and has a ready ability that
        // stops this enemy (not immune).
        bool HasReadyInterrupt(PartyMemberFacts const* m, EnemyFacts const& enemy)
        {
            if (!m || !m->CanAct() || m->casting)
                return false;
            return std::any_of(m->interrupts.begin(), m->interrupts.end(),
                [&enemy](InterruptAbility const& a) { return a.ready && CastValues::CanStop(a, enemy); });
        }
    }

    bool InterruptBehavior::AppliesTo(PartyBotAI const* bot) const
    {
        // A learned interrupt of the class kit (a priest without Silence has none).
        std::vector<SpellEntry const*> const spells = bot->Kit().Interrupts();
        return std::any_of(spells.begin(), spells.end(), [](SpellEntry const* s) { return s != nullptr; });
    }

    void InterruptBehavior::Propose(PartyBotAI const* bot, WorldModel const& world,
                                    TeamPlan const& plan, std::vector<Proposal>& out) const
    {
        Player* me = bot->me;
        ObjectGuid const self = me->GetObjectGuid();
        PartyMemberFacts const* myFacts = world.FindMember(self);
        if (!myFacts || !myFacts->CanAct())
            return;

        std::vector<Proposal> made;
        for (InterruptDuty const& duty : plan.interrupts)
        {
            bool const isPrimary = duty.primary == self;
            bool const isBackup = duty.backup == self;
            if (!isPrimary && !isBackup)
                continue;

            Unit* enemy = me->GetMap()->GetUnit(duty.enemy);
            EnemyFacts const* facts = world.FindEnemy(duty.enemy);
            if (!enemy || !enemy->IsAlive() || !facts)
                continue;

            LiveCast cast;
            if (!IsCastingInterruptible(enemy, cast))
                continue;
            EnemyFacts const live = LiveFacts(*facts, cast);

            if (!isPrimary)
            {
                // Primary asleep, stunned, gone or without a usable ability: act at once.
                bool const primaryUnable = duty.primary.IsEmpty() || !HasReadyInterrupt(world.FindMember(duty.primary), live);
                if (!primaryUnable && cast.progress < BACKUP_AFTER_PROGRESS)
                    continue;
            }

            CastValue const value = CastValues::InterruptBenefit(world, live);
            ProposalTier const tier = value.lethal ? ProposalTier::Urgent : ProposalTier::Assigned;

            // Every ready ability, assigned one first, then cheaper cooldowns: the arbiter tries
            // them in order of benefit - cost if one fails (out of range, immune, wrong form).
            std::vector<InterruptAbility> abilities = myFacts->interrupts;
            uint32 const assigned = isPrimary ? duty.primarySpellId : duty.backupSpellId;
            std::stable_sort(abilities.begin(), abilities.end(), [assigned](InterruptAbility const& a, InterruptAbility const& b)
            {
                if ((a.spellId == assigned) != (b.spellId == assigned))
                    return a.spellId == assigned;
                return a.cooldownMs < b.cooldownMs;
            });

            float preference = 0.0f;    // tie-break inside equal values: assigned first
            bool anyMelee = false;
            for (InterruptAbility const& a : abilities)
            {
                if (!a.ready || !CastValues::CanStop(a, live))
                    continue;
                SpellEntry const* spell = sSpellMgr.GetSpellEntry(a.spellId);
                if (!spell)
                    continue;
                anyMelee = anyMelee || a.maxRange <= MELEE_ABILITY_RANGE;

                Proposal p;
                p.tier = tier;
                p.source = "interrupt";
                p.reason = isPrimary ? "primary" : "backup";
                p.action.type = BotActionType::Cast;
                p.action.target = duty.enemy;
                p.action.spell = spell;
                // The school lockout is part of what this ability buys (Counterspell 10 s > Kick 5 s).
                p.SetValue(value.points + CastValues::LockoutBenefit(world, live, a, value.spellId),
                           CastValues::InterruptCost(world, *myFacts, a, live, value) + preference);
                made.push_back(p);
                preference += 0.01f;
            }

            // Melee primary not in reach: go to the caster (original chase logic moves us).
            if (isPrimary && anyMelee && bot->GetRole() != ROLE_TANK && me->GetVictim() != enemy &&
                !me->CanReachWithMeleeAutoAttack(enemy))
            {
                InterruptAbility const* melee = nullptr;
                for (InterruptAbility const& a : abilities)
                    if (a.ready && a.maxRange <= MELEE_ABILITY_RANGE && CastValues::CanStop(a, live) &&
                        (!melee || a.cooldownMs < melee->cooldownMs))
                        melee = &a;
                Proposal p;
                p.tier = tier;
                p.source = "interrupt";
                p.reason = "go to caster";
                p.action.type = BotActionType::Attack;
                p.action.target = duty.enemy;
                // Walking in is part of InterruptCost; a little extra so a ranged option wins ties.
                float const benefit = value.points + (melee ? CastValues::LockoutBenefit(world, live, *melee, value.spellId) : 0.0f);
                p.SetValue(benefit, (melee ? CastValues::InterruptCost(world, *myFacts, *melee, live, value) : benefit) + 0.05f);
                made.push_back(p);
            }
        }

        out.insert(out.end(), made.begin(), made.end());
    }

    bool InterruptBehavior::TakesOver(Takeover what, PartyBotAI const* bot, WorldModel const& /*world*/) const
    {
        // Duels are not ours (the arbiter does not run there either).
        return what == Takeover::Interrupt && !bot->IsInDuel();
    }
}
